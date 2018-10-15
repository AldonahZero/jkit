#include "request_api_server.h"

RequestApiServer::RequestApiServer(int thread_count, string listen_address, int listen_port) :
   m_thread_count(thread_count), m_work(new io_context_work(m_io_cxt.get_executor())), m_accept(m_io_cxt)
{
    m_listen_ep = tcp::endpoint{boost::asio::ip::address::from_string(listen_address), (uint16_t)listen_port};
}

RequestApiServer::~RequestApiServer()
{

}

void RequestApiServer::set_socket_opt(tcp::socket& socket)
{
    boost::asio::socket_base::keep_alive opt_keep_alive(true);
    socket.set_option(opt_keep_alive);
}

void RequestApiServer::session(tcp::socket& socket)
{
    set_socket_opt(socket);

    bool close = false;
    boost::system::error_code ec;

    // This buffer is required to persist across reads
    boost::beast::flat_buffer buffer;

    RequestContext cxt;
    cxt.remote_endpoint = socket.remote_endpoint(ec);
    if(ec)
    {
        LogErrorExt << ec.message();
        return;
    }

    boost::fibers::future<boost::system::error_code> f;
    while(m_running)
    {
        // Read a request
        StrRequest req;
        f = http::async_read(socket, buffer, req, boost::asio::fibers::use_future([](boost::system::error_code ec, size_t n) -> boost::system::error_code {
                                 return ec;
                             }));
        ec = f.get();
        if(ec == http::error::end_of_stream)
        {
            break;
        }
        if(ec)
        {
            LogErrorExt << ec.message();
            return;
        }

        cxt.req = std::move(req);
        // Send the response
        handle_request(cxt);
        close = cxt.res.need_eof();
        f = http::async_write(socket, cxt.res, boost::asio::fibers::use_future([](boost::system::error_code ec, size_t n) {
                                  return ec;
                              }));
        ec = f.get();
        if(ec)
        {
            LogErrorExt << ec.message();
            return;
        }
        if(close)
        {
            // This means we should close the connection, usually because
            // the response indicated the "Connection: close" semantic.
            break;
        }
    }

    // Send a TCP shutdown
    socket.shutdown(tcp::socket::shutdown_send, ec);

    // At this point the connection is closed gracefully
}

void RequestApiServer::accept()
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
                        LogErrorExt << e.what() << "," << typeid(e).name();
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
        LogErrorExt << e.what() << "," << typeid(e).name();
        throw e;
    }
}

void RequestApiServer::start()
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

void RequestApiServer::stop()
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

void RequestApiServer::handle_request(RequestContext& cxt)
{
    cxt.res = {};
    cxt.req_body.clear();
    cxt.res_body.clear();
    StrRequest& req = cxt.req;
    // Returns a bad request response
    auto const bad_request =
            [&req](boost::beast::string_view why)
    {
        http::response<http::string_body> res{http::status::bad_request, req.version()};
        res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
        res.set(http::field::content_type, "text/html");
        res.keep_alive(req.keep_alive());
        res.body() = why.to_string();
        res.prepare_payload();
        return res;
    };

    // Returns a not found response
    auto const not_found =
            [&req](boost::beast::string_view target)
    {
        http::response<http::string_body> res{http::status::not_found, req.version()};
        res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
        res.set(http::field::content_type, "text/html");
        res.keep_alive(req.keep_alive());
        res.body() = "The resource '" + target.to_string() + "' was not found.";
        res.prepare_payload();
        return res;
    };

    LogDebug << "http req:" << req;
    // Request path must be absolute and not contain "..".
    if( req.target().empty() ||
            req.target()[0] != '/' ||
            req.target().find("..") != boost::beast::string_view::npos)
    {
        cxt.res = bad_request("Illegal request-target");
        return;
    }

    string query_string;
    if(!kkurl::parse_target(req.target(), cxt.path, query_string))
    {
        cxt.res = bad_request("Parse target failed");
        return;
    }

    cxt.query_params = kkurl::parse_query_string(query_string);
    if(!req.body().empty() && parse_body)
    {
        if(!parse_body(req, cxt.req_body))
        {
            cxt.res = bad_request("Parse body failed");
            return;
        }
    }

    try
    {
        if(!call_resource(cxt))
        {
            auto it = default_resource.find(req.method());
            if(it != default_resource.end())
            {
                auto& fun = it->second;
                fun(cxt);
                return;
            }
            else
            {
                cxt.res = not_found(req.method_string().to_string() + " " + req.target().to_string());
                return;
            }
        }
        if(!cxt.res_body.empty() && save_body)
        {
            if(!save_body(cxt.res, cxt.res_body))
            {
                cxt.res = bad_request("Save body failed");
                return;
            }
        }
    }
    catch(std::exception& e)
    {
        LogErrorExt << e.what() << "," << typeid(e).name();
        cxt.res = bad_request(e.what());
        LogDebugExt << "http res:" << cxt.res;
        return;
    }

    cxt.res.version(req.version());
    cxt.res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
    cxt.res.content_length(cxt.res.body().size());
    cxt.res.keep_alive(req.keep_alive());
    LogDebug << "http res:" << cxt.res;
}

bool RequestApiServer::call_resource(RequestContext &cxt)
{
    auto method = cxt.req.method();
    for(auto &regex_method : resource)
    {
        auto it = regex_method.second.find(method);
        if(it != regex_method.second.end())
        {
            boost::smatch sm_res;
            if(boost::regex_match(cxt.path, sm_res, regex_method.first))
            {
                cxt.path_params = std::move(sm_res);
                auto& fun = it->second;
                fun(cxt);
                return true;
            }
        }
    }

    return false;
}
