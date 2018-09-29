#ifndef TEST_API_H
#define TEST_API_H

#include "json.hpp"
#include "ws_call_session.h"
using namespace nlohmann;

class TestServer;

class TestSession : public WsCallSession
{
public:
    TestSession(WsSessionContext& cxt, tcp::socket &s, TestServer& svr);

    virtual ~TestSession();

    json send_json_call(json req)
    {
        string res = send_call(req.dump());
        return json::parse(res);
    }

    void test_async_send_call()
    {
        //测试异步
        auto self = shared_from_this();
        boost::fibers::fiber([self, this](){
            try
            {
                json req_json;
                req_json["server"] = "this ia a server response";
                json send_res = send_json_call(req_json);
                LogDebug << send_res;
            }
            catch(std::exception& e)
            {
                LogErrorExt << e.what();
            }
        }).detach();
    }

    void test_send_call()
    {
        try
        {
            json req_json;
            req_json["server"] = "this ia a server response";
            json send_res = send_json_call(req_json);
            LogDebug << send_res;
        }
        catch(std::exception& e)
        {
            LogErrorExt << e.what();
        }
    }

private:
    json on_recv_json_call(json req)
    {
        test_async_send_call();
        //test_send_call();

        json res;
        res["server"] = "this ia a server response";
        return std::move(res);
    }

protected:
    virtual bool is_allow(const WsSessionContext& cxt)
    {
        LogDebug << cxt.path;
        return true;
    }

    virtual CaseInsensitiveMultimap add_res_header(const WsSessionContext& cxt)
    {
        return CaseInsensitiveMultimap();
    }

    virtual void on_close(const WsSessionContext& cxt)
    {
        LogDebug << "client closed";
    }

    virtual void on_error(const WsSessionContext& cxt)
    {

    }

    virtual string on_recv_call(string req_body)
    {
        LogDebug << "recv:" << req_body;
        json req_json = json::parse(req_body);
        json res = on_recv_json_call(req_json);
        return res.dump();
    }

    virtual string on_recv_busy(string req_body)
    {
        return "call busy";
    }

private:
    TestServer& m_svr;
};


class TestServer
{
    friend class TestSession;

public:
    TestServer(string listen_address, int listen_port) :
        m_server(3, listen_address, listen_port)
    {
        m_running = false;
        m_server.create_session = [this](WsSessionContext& cxt, tcp::socket& s) mutable
        {
            return WsSessionPtr(new TestSession(cxt, s, *this));
        };
    }

    ~TestServer() = default;

    void start()
    {
        m_running = true;
        m_server.start();
        boost::fibers::fiber f([this](){
            while(m_running)
            {
                boost::this_fiber::sleep_for(std::chrono::seconds(3));
                {
                    std::lock_guard<boost::fibers::mutex> lk(m_sessions_mutex);
                    LogDebug << "current session num:" << m_session_number;
                }
            }
        });

        m_f.swap(f);
    }

    void stop()
    {
        m_server.stop();
        m_running = false;
        if(m_f.joinable())
        {
            m_f.join();
        }
    }


private:
    WsServer m_server;
    boost::fibers::mutex m_sessions_mutex;
    int m_session_number = 0;
    atomic_bool m_running;
    boost::fibers::fiber m_f;
};

TestSession::TestSession(WsSessionContext& cxt, tcp::socket &s, TestServer& svr) : WsCallSession(cxt, s), m_svr(svr)
{
    std::lock_guard<boost::fibers::mutex> lk(m_svr.m_sessions_mutex);
    ++m_svr.m_session_number;
}

TestSession::~TestSession()
{
    std::lock_guard<boost::fibers::mutex> lk(m_svr.m_sessions_mutex);
    --m_svr.m_session_number;
}


#endif
