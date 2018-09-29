#ifndef MULTI_CLIENT_HTTP_HPP
#define MULTI_CLIENT_HTTP_HPP

#include <string>
#include <thread>
#include <list>
#include <map>
#include <memory>
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/asio/ssl/error.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/fiber/all.hpp>
using std::string;
using std::list;
using std::map;

#include "web_utility.hpp"
#include "exception_trace.hpp"

using tcp = boost::asio::ip::tcp;       // from <boost/asio/ip/tcp.hpp>
using namespace boost::beast;    // from <boost/beast/http.hpp>
using boost::posix_time::ptime;
namespace ssl = boost::asio::ssl;

namespace nghttp2 {
namespace asio_http2 {
namespace client {
class session;
}
}
}

struct HttpConnection
{
    string host;
    string port;
    std::shared_ptr<tcp::socket> socket_ptr;
    std::shared_ptr<boost::asio::steady_timer> timer_ptr;
    int dns_timeout = 5;
    int conn_timeout = 3;
    int req_timeout = 6;
    bool in_use = false;
    ptime last_use;
};

typedef std::shared_ptr<HttpConnection> HttpConnectionPtr;

struct HttpsConnection
{
    string host;
    string port;
    std::shared_ptr<ssl::stream<tcp::socket>> stream_ptr;
    std::shared_ptr<boost::asio::steady_timer> timer_ptr;
    int dns_timeout = 5;
    int conn_timeout = 3;
    int req_timeout = 6;
    bool in_use = false;
    ptime last_use;
};

typedef std::shared_ptr<HttpsConnection> HttpsConnectionPtr;

struct Http2sConnection
{
    string host;
    string port;
    std::shared_ptr<nghttp2::asio_http2::client::session> session_ptr;
    ptime last_use;
};

struct Http2Resopnse
{
    int status;
    int body_size;
    string body;
};

typedef http::request<http::string_body> StrRequest;
typedef http::response<http::string_body> StrResponse;

class MultiClientHttp
{
public:
    MultiClientHttp(int thread_count = 1);


    //http请求
    string request(const string& host, const string& port, boost::beast::http::verb method, const string& target, const string& body);

    StrResponse request(const string& host, const string& port, StrRequest &req);


    //https请求

    //需要确认证书
    string request(const string& host, const string& port, boost::asio::ssl::context::method ssl_method, const string& cert,
                   boost::beast::http::verb method, const string& target, const string& body);

    StrResponse request(const string& host, const string& port, boost::asio::ssl::context::method ssl_method, const string& cert, StrRequest &req);

    //不用确认证书
    StrResponse request(const string& host, const string& port, boost::asio::ssl::context::method ssl_method, StrRequest &req);

    //双向确认

    //http/2 ssl请求
    Http2Resopnse request2(const string& host, const string& port, boost::asio::ssl::context::method ssl_method, const string& cert,
                          boost::beast::http::verb method, const string& target, const std::map<string,string>& headers, const string& body);

    //不用确认证书
    Http2Resopnse request2(const string& host, const string& port, boost::asio::ssl::context::method ssl_method,
                          boost::beast::http::verb method, const string& target, const std::map<string,string>& headers, const string& body);

private:
    //http1.1请求缓存操作
    HttpConnectionPtr get_http_connect(const string& host, const string& port);
    void release_http_connect(HttpConnectionPtr conn_ptr);
    void delete_invalid_http_connect(HttpConnectionPtr conn_ptr);
    void delete_timeout_http_connect();

    //https 1.1请求缓存操作
    HttpsConnectionPtr get_https_connect(const string& host, const string& port, boost::asio::ssl::context::method ssl_method,
                                                                    const string& cert);
    void release_https_connect(HttpsConnectionPtr stream_ptr);
    void delete_invalid_https_connect(HttpsConnectionPtr stream_ptr);
    void delete_timeout_https_connect();

    //https 2 请求缓存操作
    std::shared_ptr<nghttp2::asio_http2::client::session> get_http2s_connect_stream(const string& host, const string& port, boost::asio::ssl::context::method ssl_method,
                                                                    const string& cert);
    //void release_http2s_connect_stream(std::shared_ptr<nghttp2::asio_http2::client::session> session_ptr);
    void delete_invalid_http2s_connect(std::shared_ptr<nghttp2::asio_http2::client::session> session_ptr);
    void delete_timeout_http2s_connect();

protected:
    int m_thread_count = 1;
    boost::asio::io_context m_io_cxt;
    typedef boost::asio::executor_work_guard<boost::asio::io_context::executor_type> io_context_work;
    std::unique_ptr<io_context_work> m_work;
     std::vector<std::thread> m_threads;

    size_t m_timeout = 3000;
    size_t m_timeout_connect = 2000;

    boost::fibers::mutex m_http_mutex;
    list<HttpConnectionPtr> m_cache_http_conns;

    boost::fibers::mutex m_https_mutex;
    list<HttpsConnectionPtr> m_cache_https_streams;

    boost::fibers::mutex m_http2s_mutex;
    list<Http2sConnection> m_cache_http2s_session;

    int m_unuse_timeout = 55; //超过55秒没有使用就断掉
};

#endif /* MULTI_CLIENT_HTTP_HPP */
