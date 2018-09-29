#ifndef WS_CALL_SERVER_H
#define WS_CALL_SERVER_H

#include <boost/beast.hpp>
#include <boost/regex.hpp>
#include <boost/asio.hpp>
#include <boost/any.hpp>
#include <boost/fiber/all.hpp>
#include <boost/unordered_map.hpp>
#include <boost/date_time.hpp>

#include <chrono>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <map>
#include <cstdio>
#include <cctype>
#include <string>
#include <memory>
#include <sstream>
#include <thread>
#include <functional>

#include "use_future.hpp"
#include "logger.h"
#include "web_utility.hpp"
#include "exception_trace.hpp"

using tcp = boost::asio::ip::tcp;       // from <boost/asio/ip/tcp.hpp>
using namespace boost::beast;
using namespace std;
using boost::asio::ip::tcp;

typedef http::request<http::string_body> StrRequest;
typedef http::response<http::string_body> StrResponse;

enum class WsCallError
{
    NETWORK_ERROR = 38001,
    TIMEOUT = 38002,
};

struct WsSessionContext
{
    boost::asio::ip::tcp::endpoint remote_endpoint; //对端ip
    StrRequest req; //http handshake请求
    string path; //不包含问号后参数
    CaseInsensitiveMultimap query_params; //url解析后的 key value参数

    WsSessionContext() = default;
    WsSessionContext(const WsSessionContext& c) = delete;
    WsSessionContext(WsSessionContext&& c);
    ~WsSessionContext() = default;

    WsSessionContext& operator =(const WsSessionContext& c) = delete;
    WsSessionContext& operator =(WsSessionContext&& c);
};

class WsSession : public std::enable_shared_from_this<WsSession>
{
public:
    WsSession(WsSessionContext& c, tcp::socket& s) {}
    virtual ~WsSession() {}

    virtual void start() { LogErrorExt << "not implemented";}
};

typedef std::shared_ptr<WsSession> WsSessionPtr;

class WsServer
{
public:
    WsServer(int thread_count, string listen_address, int listen_port);
    virtual ~WsServer();

    void start();
    void stop();

public:
    std::function<WsSessionPtr(WsSessionContext&, tcp::socket&)> create_session;

private:
    /*****************************************************************************
    *   fiber function per server connection
    *****************************************************************************/
    void session(tcp::socket &socket);

    void accept();

private:
    int m_thread_count = 1;
    boost::asio::io_context m_io_cxt;
    typedef boost::asio::executor_work_guard<boost::asio::io_context::executor_type> io_context_work;
    std::unique_ptr<io_context_work> m_work;
    tcp::acceptor m_accept;
    std::vector<std::thread> m_threads;
};

#endif
