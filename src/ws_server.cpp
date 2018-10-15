#include "ws_server.h"
#include "ws_call_session.h"

WsServer::WsServer(int thread_count, string listen_address, int listen_port) :
   m_thread_count(thread_count), m_work(new io_context_work(m_io_cxt.get_executor())), m_accept(m_io_cxt)
{
    m_listen_ep = tcp::endpoint{boost::asio::ip::address::from_string(listen_address), (uint16_t)listen_port};
    create_session = [](WsSessionContext& c, tcp::socket& s) mutable ->WsSessionPtr  {
        return std::make_shared<WsSession>(c, s);
    };
}

WsServer::~WsServer()
{

}

void WsServer::start()
{
    m_io_cxt.restart();
    m_running = true;
    {
        std::lock_guard<boost::fibers::mutex> lk(m_session_mutex);
        m_session_number = 0;
    }

    m_accept_fiber = boost::fibers::fiber([this](){
        this->accept();
    });

    for(int i=0; i<m_thread_count; ++i)
    {
        std::thread t([this]() {
            m_io_cxt.run();
        });
        m_threads.push_back(std::move(t));
    }
}

void WsServer::stop()
{
    m_running = false;
    boost::system::error_code ec;
    m_accept.close(ec);
    if(m_accept_fiber.joinable())
    {
        m_accept_fiber.join();
    }

    {
        std::unique_lock<boost::fibers::mutex> lk(m_session_mutex);
        m_session_cnd.wait(lk, [this](){
            return m_session_number == 0;
        });
    }

    m_io_cxt.stop();
    for(int i=0; i<m_thread_count; ++i)
    {
        m_threads[i].join();
    }
}

void WsServer::session(tcp::socket& socket)
{
    boost::system::error_code ec;
    boost::fibers::future<boost::system::error_code> f;
    boost::beast::flat_buffer buffer;

    StrRequest req;
    f = http::async_read(socket, buffer, req, boost::asio::fibers::use_future([](boost::system::error_code ec, size_t n) {
                             return ec;
                         }));
    ec = f.get();
    if(ec == http::error::end_of_stream)
    {
        socket.shutdown(tcp::socket::shutdown_send, ec);
        return;
    }
    if(ec)
    {
        LogErrorExt << ec.message() << "\nremote_ip:" << socket.remote_endpoint(ec);
        return;
    }

    if(!websocket::is_upgrade(req))
    {
        LogErrorExt << "is not upgrade:" << req << "\nremote_ip:" << socket.remote_endpoint(ec);
        socket.shutdown(tcp::socket::shutdown_send, ec);
        return;
    }

    WsSessionContext cxt;
    string query_string;
    if(!kkurl::parse_target(req.target(), cxt.path, query_string))
    {
        LogErrorExt << "not parse target:" << req.target() << "\nremote_ip:" << socket.remote_endpoint(ec);
        socket.shutdown(tcp::socket::shutdown_send, ec);
        return;
    }

    cxt.query_params = kkurl::parse_query_string(query_string);
    cxt.remote_endpoint = socket.remote_endpoint(ec);
    //cxt.ws = websocket::stream<tcp::socket>(std::move(socket));
    cxt.req = std::move(req);

    WsSessionPtr ses = create_session(cxt, socket);
    ses->start();
}

void WsServer::accept()
{
    try
    {
        m_accept.open(m_listen_ep.protocol());
        m_accept.set_option(boost::asio::ip::tcp::acceptor::reuse_address(true));
        m_accept.bind(m_listen_ep);
        m_accept.listen();

        boost::fibers::future<boost::system::error_code> f;
        boost::system::error_code ec;
        for (;;)
        {
            tcp::socket socket(m_io_cxt);
            f = m_accept.async_accept(socket,
                                      boost::asio::fibers::use_future([](boost::system::error_code ec){
                                          return ec;
                                      }));
            ec = f.get();
            if (ec)
            {
                if(ec.value() == boost::asio::error::no_descriptors)
                {
                    LogErrorExt << ec.message();
                    continue;
                }
                else if(ec.value() == boost::asio::error::operation_aborted) //主动关闭结束
                {
                    LogWarnExt << ec.message();
                    break;
                }
                else
                {
                    throw_with_trace(boost::system::system_error(ec)); //some other error
                }
            }
            else
            {
                boost::fibers::fiber([s = std::move(socket), this]() mutable {
                    try
                    {
                        this->session(s);
                    }
                    catch (std::exception const &e)
                    {
                        LogErrorExt << e.what() << "," << typeid(e).name();;
                    }
                    std::lock_guard<boost::fibers::mutex> lk(m_session_mutex);
                    --m_session_number;
                    if(!m_running && m_session_number == 0)
                    {
                        m_session_cnd.notify_one();
                    }
                }).detach();

                std::lock_guard<boost::fibers::mutex> lk(m_session_mutex);
                ++m_session_number;
            }
        }
    }
    catch (std::exception const &e)
    {
        LogErrorExt << e.what() << "," << typeid(e).name();;
        throw e;
    }
}

WsSessionContext::WsSessionContext(WsSessionContext&& c)
{
    if(this == &c)
        return;
    remote_endpoint = std::move(c.remote_endpoint);
    req = std::move(c.req);
    path = std::move(c.path);
    query_params = std::move(c.query_params);
}

WsSessionContext& WsSessionContext::operator =(WsSessionContext&& c)
{
    if(this == &c)
        return *this;

    remote_endpoint = std::move(c.remote_endpoint);
    req = std::move(c.req);
    path = std::move(c.path);
    query_params = std::move(c.query_params);
}
