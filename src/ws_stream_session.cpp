#include "ws_stream_session.h"


WsStreamSession::WsStreamSession(WsSessionContext& cxt, tcp::socket& s) :
    WsSession(cxt, s),
    m_cxt(std::move(cxt)),
    m_ws(std::move(s))
{

}

WsStreamSession::~WsStreamSession()
{

}

void WsStreamSession::start()
{
    if(!process_handshake())
    {
        return;
    }

    set_socket_opt();

    {
        std::lock_guard<boost::fibers::mutex> lk(m_mutex);
        m_running = true;
    }
    m_ws.binary(true);

    boost::fibers::fiber fsend([this](){
        this->process_send_data();
    });

    m_send_fiber.swap(fsend);


    boost::fibers::fiber frecv([this](){
        this->process_recv_data();
    });

    m_recv_fiber.swap(frecv);
    m_recv_fiber.join();

    close();

    {
        std::lock_guard<boost::fibers::mutex> lk(m_mutex);
        m_running = false;
        m_cnd.notify_all();
    }
}

void WsStreamSession::stop()
{
    boost::system::error_code ec;
    m_ws.next_layer().shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);
    {
        std::unique_lock<boost::fibers::mutex> lk(m_mutex);
        m_cnd.wait_for(lk, std::chrono::seconds(5), [this](){
            return !m_running;
        });
    }
}

void WsStreamSession::close()
{
    if(m_send_fiber.joinable())
    {
        m_send_fiber.join();
    }
}

void WsStreamSession::set_socket_opt()
{
    auto& socket = m_ws.next_layer();
    boost::asio::socket_base::keep_alive opt_keep_alive(true);
    socket.set_option(opt_keep_alive);

    //异步模式下 no delay会导致socket出现operation_aborted错误
    // boost::asio::ip::tcp::no_delay no_delay_option(true);
    // socket.set_option(no_delay_option);

    boost::system::error_code ec;
    socket.non_blocking(true, ec);
    if(ec)
    {
        LogErrorExt << ec.message();
    }
}

void WsStreamSession::send(boost::any pkt)
{
    std::lock_guard<boost::fibers::mutex> lk(m_send_mutex);
    m_send_buffer.push(std::move(pkt));
}

void WsStreamSession::process_recv_data()
{
    string recv_buf;
    boost::fibers::future<boost::system::error_code> f;
    boost::system::error_code ec;

    for(;;)
    {
        boost::beast::flat_buffer buffer;
        f = m_ws.async_read(buffer, boost::asio::fibers::use_future([](boost::system::error_code ec, size_t n) {
                              return ec;
                          }));
        ec = f.get();
        if(ec == boost::beast::websocket::error::closed || ec == boost::asio::error::eof)
        {
            on_close(m_cxt);
            break;
        }

        if(ec)
        {
            LogErrorExt << ec.message() << "\nremote_ip:" << m_cxt.remote_endpoint;
            m_ws.next_layer().shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);
            on_error(m_cxt);
            break;
        }

        recv_buf = boost::beast::buffers_to_string(buffer.data());
        on_recv_data(std::move(recv_buf));
    }
}

bool WsStreamSession::process_handshake()
{
    boost::system::error_code ec;
    if(!is_allow(m_cxt))
    {
        m_ws.next_layer().shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);
        return false;
    }

    boost::fibers::future<boost::system::error_code> f;
    CaseInsensitiveMultimap custom_header = add_res_header(m_cxt);

    auto res_decorator = [&custom_header](http::response<http::string_body>& upgrade_response) {
        for(auto& p : custom_header)
        {
            upgrade_response.insert(p.first, p.second);
        }
    };

    f = m_ws.async_accept_ex(m_cxt.req, res_decorator, boost::asio::fibers::use_future([](boost::system::error_code ec) {
                               return ec;
                           }));
    ec = f.get();
    if(ec)
    {
        LogErrorExt << ec.message() << "\nremote_ip:" << m_cxt.remote_endpoint;
        on_error(m_cxt);
        return false;
    }
    return true;
}

void WsStreamSession::process_send_data()
{
    boost::system::error_code ec;
    bool is_empty;
    boost::any* pkt = nullptr;
    while(1)
    {
        {
            std::lock_guard<boost::fibers::mutex> lk(m_send_mutex);
            is_empty = m_send_buffer.empty();
        }
        if(is_empty)
        {
            on_send_empty();
            continue;
        }

        {
            std::lock_guard<boost::fibers::mutex> lk(m_send_mutex);
            pkt = &m_send_buffer.front();
        }
        const string& data = get_send_data(pkt);
        if(data.empty())
        {
            continue;
        }
        m_ws.write(boost::asio::buffer(data), ec);
        if(!ec) //没有错误
        {
            std::lock_guard<boost::fibers::mutex> lk(m_send_mutex);
            m_send_buffer.pop();
            continue;
        }

        if (ec == boost::asio::error::would_block) //如果堵塞
        {
            on_send_block();
            continue;
        }
        else
        {
            LogWarn << ec.message() << "," << m_cxt.remote_endpoint;
            break;
        }
    }
}

const string& WsStreamSession::get_send_data(const boost::any* pkt)
{
    const string* data_ptr = boost::any_cast<std::string>(pkt);
//    if(nullptr == data_ptr)
//        return "";

    return *data_ptr;
}

void WsStreamSession::on_send_empty()
{
    boost::this_fiber::sleep_for(std::chrono::milliseconds(20));
}

