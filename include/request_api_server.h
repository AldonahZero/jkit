#ifndef REQUEST_API_SERVER_H
#define REQUEST_API_SERVER_H

#include <boost/beast.hpp>
#include <boost/regex.hpp>
#include <boost/asio.hpp>
#include <boost/any.hpp>
#include <boost/fiber/all.hpp>

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

struct RequestContext
{
    boost::asio::ip::tcp::endpoint remote_endpoint;
    string path; //不包含问号后参数
    boost::smatch path_params;
    CaseInsensitiveMultimap query_params;
    boost::any req_body;

    StrRequest req;
    StrResponse res;
    boost::any res_body;
};

class RequestApiServer
{
public:
    RequestApiServer(int thread_count, string listen_address, int listen_port);
    virtual ~RequestApiServer();

    void start();
    void stop();

private:
    class RegexOrderable : public boost::regex {
        std::string str;

    public:
        RegexOrderable(const char *regex_cstr) : boost::regex(regex_cstr), str(regex_cstr) {}
        RegexOrderable(std::string regex_str) : boost::regex(regex_str), str(std::move(regex_str)) {}
        bool operator<(const RegexOrderable &rhs) const noexcept {
            return str < rhs.str;
        }
    };

public:
    typedef std::function<void (RequestContext&)> ResourceCall;
    std::map<RegexOrderable, std::map<http::verb, ResourceCall>> resource;
    std::map<http::verb, ResourceCall> default_resource;

    //用于body解析,默认为nullptr,不处理
    std::function<bool (StrRequest& req, boost::any& body)> parse_body;
    std::function<bool (StrResponse& res, boost::any& body)> save_body;

    //any当作string处理
    /*
        parse_body = [](StrRequest& req, boost::any& body) -> bool {
            body = std::move(req.body());
            return true;
        };

        save_body = [](StrResponse& res, boost::any& body) -> bool {
            string* body_ptr = boost::any_cast<std::string>(&body);
            if(nullptr == body_ptr)
                return false;
            res.body() = std::move(*body_ptr);
            return true;
        };
        */

    //any当作json处理
    /*
    parse_body = [](StrRequest& req, boost::any& body) ->bool {
        try
        {
            body = nlohmann::json::parse(req.body());
            return true;
        }
        catch (nlohmann::json::exception &e)
        {
            LogErrorExt << e.what();
        }
        return false;
    }

    save_body = [](StrResponse& res, boost::any& body) ->bool {
        nlohmann::json* body_ptr = boost::any_cast<nlohmann::json>(&body);
        if(nullptr == body_ptr)
            return false;
        res.body() = body_ptr->dump();
        return true;
    }
    */

private:
    void handle_request(RequestContext &cxt);

    //返回false表示没找到资源函数
    bool call_resource(RequestContext &cxt);

    /*****************************************************************************
    *   fiber function per server connection
    *****************************************************************************/
    void session(tcp::socket& socket);

    void accept();

    void set_socket_opt(tcp::socket &socket);
private:
    int m_thread_count = 1;
    boost::asio::io_context m_io_cxt;
    typedef boost::asio::executor_work_guard<boost::asio::io_context::executor_type> io_context_work;
    std::unique_ptr<io_context_work> m_work;
    tcp::acceptor m_accept;
    std::vector<std::thread> m_threads;

    boost::fibers::fiber m_accept_fiber;
    tcp::endpoint m_listen_ep;
    boost::fibers::mutex m_session_mutex;
    int m_session_number = 0;
    boost::fibers::condition_variable_any m_session_cnd;
    atomic_bool m_running;
};

#endif
