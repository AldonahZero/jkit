#include "multi_client_http.h"
#include <iostream>
#include "nghttp2/asio_http2_client.h"
#include "logger.h"
#include "use_future.hpp"

MultiClientHttp::MultiClientHttp(int thread_count) :
    m_thread_count(thread_count), m_work(new io_context_work(m_io_cxt.get_executor()))
{
    boost::fibers::fiber([this](){
        while(1)
        {
            delete_timeout_http_connect();
            delete_timeout_https_connect();
            delete_timeout_http2s_connect();
            boost::this_fiber::sleep_for(std::chrono::seconds(30));
        }
    }).detach();

    for(int i=0; i<m_thread_count; ++i)
    {
        std::thread t([this]() {
            m_io_cxt.run();
        });
        m_threads.push_back(std::move(t));
    }
}

HttpConnectionPtr MultiClientHttp::get_http_connect(const string& host, const string& port)
{
    {
        std::lock_guard<boost::fibers::mutex> lk{m_http_mutex};
        for(auto it=m_cache_http_conns.begin(); it!=m_cache_http_conns.end(); ++it)
        {
            HttpConnectionPtr& ptr = *it;
            if(!ptr->in_use && ptr->host == host && ptr->port == port)
            {
                ptr->in_use = true;
                ptr->last_use = boost::posix_time::second_clock::local_time();
                return ptr;
            }
        }
    }

    HttpConnectionPtr conn_ptr(new HttpConnection());
    HttpConnection& conn = *conn_ptr;

    conn.host = host;
    conn.port = port;

    std::shared_ptr<boost::asio::steady_timer> timer = std::make_shared<boost::asio::steady_timer>(m_io_cxt);
    conn.timer_ptr = timer;

    //dns查询
    tcp::resolver resolver(m_io_cxt);
    timer->expires_after(std::chrono::seconds(conn.dns_timeout));
    timer->async_wait([&resolver](boost::system::error_code ec){
        if(!ec) {
            resolver.cancel();
        }
    });

    auto lookup_future = resolver.async_resolve({conn.host, conn.port}, boost::asio::fibers::use_future);
    auto lookup = lookup_future.get();
    timer->cancel();

    std::shared_ptr<tcp::socket> socket_ptr = std::make_shared<tcp::socket>(m_io_cxt);
    conn.socket_ptr = socket_ptr;

    //连接
    timer->expires_after(std::chrono::seconds(conn.conn_timeout));
    timer->async_wait([conn_ptr](boost::system::error_code ec){
        if(!ec) {
            conn_ptr->socket_ptr->lowest_layer().cancel(ec);
        }
    });
    auto endpoint_future = boost::asio::async_connect(*socket_ptr, lookup, boost::asio::fibers::use_future);
    endpoint_future.get();
    timer->cancel();

    {
        conn.in_use = true;
        conn.last_use = boost::posix_time::second_clock::local_time();

        std::lock_guard<boost::fibers::mutex> lk{m_http_mutex};
        m_cache_http_conns.push_back(conn_ptr);

        return conn_ptr;
    }
}

void MultiClientHttp::release_http_connect(HttpConnectionPtr conn_ptr)
{
    std::lock_guard<boost::fibers::mutex> lk{m_http_mutex};
    for(auto it=m_cache_http_conns.begin(); it!=m_cache_http_conns.end(); ++it)
    {
        HttpConnectionPtr& ptr = *it;
        if(ptr.get() == conn_ptr.get())
        {
            ptr->in_use = false;
            ptr->last_use = boost::posix_time::second_clock::local_time();
            return;
        }
    }
}

void MultiClientHttp::delete_invalid_http_connect(HttpConnectionPtr conn_ptr)
{
    std::lock_guard<boost::fibers::mutex> lk{m_http_mutex};
    for(auto it=m_cache_http_conns.begin(); it!=m_cache_http_conns.end(); ++it)
    {
        HttpConnectionPtr& ptr = *it;
        if(ptr.get() == conn_ptr.get())
        {
            m_cache_http_conns.erase(it);
            return;
        }
    }
}

void MultiClientHttp::delete_timeout_http_connect()
{
    ptime now_pt = boost::posix_time::second_clock::local_time();
    std::lock_guard<boost::fibers::mutex> lk{m_http_mutex};
    for(auto it=m_cache_http_conns.begin(); it!=m_cache_http_conns.end();)
    {
        HttpConnectionPtr& ptr = *it;
        if(!ptr->in_use)
        {
            auto diff = now_pt - ptr->last_use;
            if (diff.total_seconds() > m_unuse_timeout)
            {
                it = m_cache_http_conns.erase(it);
            }
            else
            {
                ++it;
            }
        }
        else
        {
            ++it;
        }
    }
}

HttpsConnectionPtr MultiClientHttp::get_https_connect(const string& host, const string& port, boost::asio::ssl::context::method ssl_method,
                                                      const string& cert)
{
    {
        std::lock_guard<boost::fibers::mutex> lk{m_https_mutex};
        for(auto it=m_cache_https_streams.begin(); it!=m_cache_https_streams.end(); ++it)
        {
            HttpsConnectionPtr& ptr = *it;
            if(!ptr->in_use && ptr->host == host && ptr->port == port)
            {
                ptr->in_use = true;
                ptr->last_use = boost::posix_time::second_clock::local_time();
                return ptr;
            }
        }
    }

    HttpsConnectionPtr conn_ptr(new HttpsConnection());
    HttpsConnection& conn = *conn_ptr;

    conn.host = host;
    conn.port = port;

    boost::system::error_code ec;
    ssl::context ctx{ssl_method};
    if(!cert.empty())
    {
        ctx.set_verify_mode(ssl::verify_peer);
        ctx.add_certificate_authority(boost::asio::buffer(cert.data(), cert.size()), ec);
        if(ec)
        {
            throw_with_trace(std::runtime_error("add_certificate:" + ec.message()));
        }
    }
    else
    {
        ctx.set_verify_mode(ssl::verify_none);
    }

    std::shared_ptr<boost::asio::steady_timer> timer = std::make_shared<boost::asio::steady_timer>(m_io_cxt);
    conn.timer_ptr = timer;

    //dns
    tcp::resolver resolver(m_io_cxt);
    timer->expires_after(std::chrono::seconds(conn.dns_timeout));
    timer->async_wait([&resolver](boost::system::error_code ec){
        if(!ec) {
            resolver.cancel();
        }
    });

    auto lookup_future = resolver.async_resolve({conn.host, conn.port}, boost::asio::fibers::use_future);
    auto lookup = lookup_future.get();
    timer->cancel();

    std::shared_ptr<ssl::stream<tcp::socket>> stream_ptr = std::make_shared<ssl::stream<tcp::socket>>(m_io_cxt, ctx);
    ssl::stream<tcp::socket>& stream = *stream_ptr;
    conn.stream_ptr = stream_ptr;

    //设置ssl
    if(!SSL_set_tlsext_host_name(stream.native_handle(), conn.host.c_str()))
    {
        throw_with_trace(std::runtime_error("set_tlsext:" + ec.message()));
    }


    //连接
    timer->expires_after(std::chrono::seconds(conn.conn_timeout));
    timer->async_wait([conn_ptr](boost::system::error_code ec){
        if(!ec) {
            conn_ptr->stream_ptr->next_layer().lowest_layer().cancel(ec);
        }
    });

    auto endpoint_future = boost::asio::async_connect(stream.next_layer(), lookup, boost::asio::fibers::use_future);
    endpoint_future.get();
    timer->cancel();

    //handshake
    timer->expires_after(std::chrono::seconds(conn.conn_timeout));
    timer->async_wait([conn_ptr](boost::system::error_code ec){
        if(!ec) {
            conn_ptr->stream_ptr->next_layer().lowest_layer().cancel(ec);
        }
    });

    auto fvoid = stream.async_handshake(ssl::stream_base::client, boost::asio::fibers::use_future);
    fvoid.get();
    timer->cancel();


    {
        conn.in_use = true;
        conn.last_use = boost::posix_time::second_clock::local_time();

        std::lock_guard<boost::fibers::mutex> lk{m_https_mutex};
        m_cache_https_streams.push_back(conn_ptr);
        return conn_ptr;
    }
}

void MultiClientHttp::release_https_connect(HttpsConnectionPtr stream_ptr)
{
    std::lock_guard<boost::fibers::mutex> lk{m_https_mutex};
    for(auto it=m_cache_https_streams.begin(); it!=m_cache_https_streams.end(); ++it)
    {
        HttpsConnectionPtr& ptr = *it;
        if(ptr.get() == stream_ptr.get())
        {
            ptr->in_use = false;
            ptr->last_use = boost::posix_time::second_clock::local_time();
            return;
        }
    }
}

void MultiClientHttp::delete_invalid_https_connect(HttpsConnectionPtr stream_ptr)
{
    std::lock_guard<boost::fibers::mutex> lk{m_https_mutex};
    for(auto it=m_cache_https_streams.begin(); it!=m_cache_https_streams.end(); ++it)
    {
        HttpsConnectionPtr& ptr = *it;
        if(ptr.get() == stream_ptr.get())
        {
            m_cache_https_streams.erase(it);
            return;
        }
    }
}

void MultiClientHttp::delete_timeout_https_connect()
{
    ptime now_pt = boost::posix_time::second_clock::local_time();
    std::lock_guard<boost::fibers::mutex> lk{m_https_mutex};
    //std::cout << "https stream size:" << m_cache_https_streams.size() << std::endl;
    for(auto it=m_cache_https_streams.begin(); it!=m_cache_https_streams.end();)
    {
        HttpsConnectionPtr& ptr = *it;
        if(!ptr->in_use)
        {
            auto diff = now_pt - ptr->last_use;
            if (diff.total_seconds() > m_unuse_timeout)
            {
                it = m_cache_https_streams.erase(it);
            }
            else
            {
                ++it;
            }
        }
        else
        {
            ++it;
        }
    }
}

std::shared_ptr<nghttp2::asio_http2::client::session> MultiClientHttp::get_http2s_connect_stream(const string& host, const string& port,
                                                                                                 boost::asio::ssl::context::method ssl_method,  const string& cert)
{
    {
        std::lock_guard<boost::fibers::mutex> lk{m_http2s_mutex};
        for(auto it=m_cache_http2s_session.begin(); it!=m_cache_http2s_session.end(); ++it)
        {
            if(it->host == host && it->port == port)
            {
                //it->in_use = true;
                it->last_use = boost::posix_time::second_clock::local_time();
                return it->session_ptr;
            }
        }
    }

    //创建http2s stream
    boost::system::error_code ec;

    ssl::context ctx{ssl_method};
    if(!cert.empty())
    {
        ctx.set_verify_mode(ssl::verify_peer);
        ctx.add_certificate_authority(boost::asio::buffer(cert.data(), cert.size()), ec);
        if(ec)
        {
            throw_with_trace(std::runtime_error("add_certificate:" + ec.message()));
        }
    }
    else
    {
        ctx.set_verify_mode(ssl::verify_none);
    }

    nghttp2::asio_http2::client::configure_tls_context(ec, ctx);
    if(ec)
    {
        throw_with_trace(std::runtime_error("configure tls context:" + ec.message()));
    }

    int conn_timeout = 10;
    boost::posix_time::time_duration td(0, 0, 10);
    std::shared_ptr<nghttp2::asio_http2::client::session> session_ptr =
            std::make_shared<nghttp2::asio_http2::client::session>(m_io_cxt, ctx, host, port, td); //10秒超时

    boost::fibers::promise<void> promise;
    boost::fibers::future<void> future(promise.get_future());

    session_ptr->on_connect([&promise](boost::asio::ip::tcp::resolver::iterator endpoint) {
        promise.set_value();
    });
    session_ptr->on_error([session_ptr, this](const boost::system::error_code &error) {
        delete_invalid_http2s_connect(session_ptr);
    });

    boost::fibers::future_status status = future.wait_for(std::chrono::seconds(conn_timeout));
    if (status == boost::fibers::future_status::timeout)
    {
        throw_with_trace(std::runtime_error("http2 ssl conn timeout"));
    }
    future.get();

    {
        //std::cout << "create http2 ssl session" << std::endl;
        Http2sConnection conn;
        conn.host = host;
        conn.port = port;
        conn.session_ptr = session_ptr;
        conn.last_use = boost::posix_time::second_clock::local_time();

        std::lock_guard<boost::fibers::mutex> lk{m_http2s_mutex};
        m_cache_http2s_session.push_back(conn);

        return session_ptr;
    }
}

void MultiClientHttp::delete_invalid_http2s_connect(std::shared_ptr<nghttp2::asio_http2::client::session> session_ptr)
{
    std::lock_guard<boost::fibers::mutex> lk{m_http2s_mutex};
    for(auto it=m_cache_http2s_session.begin(); it!=m_cache_http2s_session.end(); ++it)
    {
        if(it->session_ptr.get() == session_ptr.get())
        {
            m_cache_http2s_session.erase(it);
            return;
        }
    }
}

void MultiClientHttp::delete_timeout_http2s_connect()
{
    ptime now_pt = boost::posix_time::second_clock::local_time();
    std::lock_guard<boost::fibers::mutex> lk{m_http2s_mutex};
    //std::cout << "http2s session size:" << m_cache_http2s_session.size() << std::endl;
    for(auto it=m_cache_http2s_session.begin(); it!=m_cache_http2s_session.end();)
    {
        auto diff = now_pt - it->last_use;
        if (diff.total_seconds() > m_unuse_timeout)
        {
            it = m_cache_http2s_session.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

string MultiClientHttp::request(const string& host, const string& port, boost::beast::http::verb method, const string& target, const string& body)
{
    StrRequest req{method, target, 11};
    req.set(http::field::host, host);
    req.content_length(body.size());
    req.body() = body;
    StrResponse res = request(host, port, req);
    return res.body();
}

StrResponse MultiClientHttp::request(const string& host, const string& port, StrRequest& req)
{
    HttpConnectionPtr conn_ptr = get_http_connect(host, port);
    tcp::socket& socket = *(conn_ptr->socket_ptr);
    boost::asio::steady_timer& timer = *(conn_ptr->timer_ptr);

    try
    {
        //发送请求
        timer.expires_after(std::chrono::seconds(conn_ptr->req_timeout));
        timer.async_wait([conn_ptr](boost::system::error_code ec){
            if(!ec) {
                conn_ptr->socket_ptr->lowest_layer().cancel(ec);
            }
        });
        boost::fibers::future<size_t> tmp_future;
        tmp_future = http::async_write(socket, req, boost::asio::fibers::use_future);
        tmp_future.get();

        //返回响应
        boost::beast::flat_buffer b;
        StrResponse res;
        tmp_future = http::async_read(socket, b, res, boost::asio::fibers::use_future);
        tmp_future.get();
        timer.cancel();
        if(res.need_eof())
        {
            boost::system::error_code ec;
            socket.shutdown(tcp::socket::shutdown_both, ec);
            delete_invalid_http_connect(conn_ptr);
        }
        else
        {
            release_http_connect(conn_ptr);
        }
        return std::move(res);
    }
    catch( boost::system::system_error& e)
    {
        if(e.code() != boost::asio::error::operation_aborted)
        {
            //非超时引起的错误
            timer.cancel();
        }
        delete_invalid_http_connect(conn_ptr);
        throw e;
    }
    catch(std::exception& e)
    {
        delete_invalid_http_connect(conn_ptr);
        throw e;
    }
}

string MultiClientHttp::request(const string& host, const std::string &port, boost::asio::ssl::context_base::method ssl_method, const string& cert,
                                http::verb method, const string& target, const string& body)
{
    StrRequest req{method, target, 11};
    req.set(http::field::host, host);
    req.content_length(body.size());
    req.body() = body;
    StrResponse res = request(host, port, ssl_method, cert, req);
    return res.body();
}

StrResponse MultiClientHttp::request(const string& host, const string& port, boost::asio::ssl::context::method ssl_method, StrRequest &req)
{
    return request(host, port, ssl_method, "", req);
}

StrResponse MultiClientHttp::request(const string& host, const string& port, boost::asio::ssl::context::method ssl_method, const string& cert, StrRequest& req)
{
    HttpsConnectionPtr conn_ptr = get_https_connect(host, port, ssl_method, cert);
    ssl::stream<tcp::socket>& stream = *(conn_ptr->stream_ptr);
    boost::asio::steady_timer& timer = *(conn_ptr->timer_ptr);

    try
    {
        //发送请求
        timer.expires_after(std::chrono::seconds(conn_ptr->req_timeout));
        timer.async_wait([conn_ptr](boost::system::error_code ec){
            if(!ec) {
                conn_ptr->stream_ptr->next_layer().lowest_layer().cancel(ec);
            }
        });

        boost::fibers::future<size_t> tmp_future;
        tmp_future = http::async_write(stream, req, boost::asio::fibers::use_future);
        tmp_future.get();

        //返回响应
        boost::beast::flat_buffer b;
        StrResponse res;
        tmp_future = http::async_read(stream, b, res, boost::asio::fibers::use_future);
        tmp_future.get();
        timer.cancel();

        if(res.need_eof())
        {
            boost::fibers::future<boost::system::error_code> ferror = stream.async_shutdown(boost::asio::fibers::use_future([](boost::system::error_code ec){
                                                                                                return ec;
                                                                                            }));
            boost::system::error_code ec = ferror.get();
            if(ec == boost::asio::error::eof)
            {
                // Rationale:
                // http://stackoverflow.com/questions/25587403/boost-asio-ssl-async-shutdown-always-finishes-with-an-error
                ec.assign(0, ec.category());
            }
            delete_invalid_https_connect(conn_ptr);
        }
        else
        {
            release_https_connect(conn_ptr);
        }

        return std::move(res);
    }
    catch( boost::system::system_error& e)
    {
        if(e.code() != boost::asio::error::operation_aborted)
        {
            //非超时引起的错误
            timer.cancel();
        }
        delete_invalid_https_connect(conn_ptr);
        throw e;
    }
    catch(std::exception& e)
    {
        delete_invalid_https_connect(conn_ptr);
        throw e;
    }
}

Http2Resopnse MultiClientHttp::request2(const string& host, const string& port, boost::asio::ssl::context::method ssl_method,
                                        boost::beast::http::verb method, const string& target, const std::map<string,string>& headers, const string& body)
{
    return request2(host, port, ssl_method, "", method, target, headers, body);
}

Http2Resopnse MultiClientHttp::request2(const string& host, const string& port, boost::asio::ssl::context::method ssl_method, const string& cert,
                                        boost::beast::http::verb method, const string& target, const std::map<string,string>& headers, const string& body)
{
    std::shared_ptr<nghttp2::asio_http2::client::session> session_ptr = get_http2s_connect_stream(host, port, ssl_method, cert);
    nghttp2::asio_http2::header_map h2_headers;
    for(auto& p : headers)
    {
        if(p.first == "Authorization")
        {
            h2_headers.emplace(p.first, nghttp2::asio_http2::header_value{p.second, true});
        }
        else
        {
            h2_headers.emplace(p.first, nghttp2::asio_http2::header_value{p.second, false});
        }
    }
    boost::system::error_code ec;
    const nghttp2::asio_http2::client::request *request = session_ptr->submit(ec,
                                                                              http::to_string(method).data(),
                                                                              "https://" + host + ":" + port + target,
                                                                              body,
                                                                              h2_headers);
    if(ec)
    {
        throw_with_trace(std::runtime_error("submit:" + ec.message()));
    }

    boost::fibers::promise<void> promise;
    boost::fibers::future<void> future(promise.get_future());
    Http2Resopnse h2_res;

    request->on_response([&h2_res, &promise, this]( const nghttp2::asio_http2::client::response &response ) {
        h2_res.status = response.status_code();
        response.on_data([&h2_res, &promise](const uint8_t *data, std::size_t len ) {
            if(len > 0)
            {
                h2_res.body = std::string((const char*)data, len);
            }
            h2_res.body_size = len;
            promise.set_value();
        });
    });
    boost::fibers::future_status status = future.wait_for(std::chrono::seconds(6));
    if (status == boost::fibers::future_status::timeout)
    {
        delete_invalid_http2s_connect(session_ptr);
        throw_with_trace(std::runtime_error("http2 response timeout"));
    }
    future.get();
    return h2_res;
}
