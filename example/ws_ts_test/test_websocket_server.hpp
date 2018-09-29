#ifndef TEST_API_H
#define TEST_API_H

#include "web_tool/ws_transport_server.h"
#include "json.hpp"


typedef std::shared_ptr<WsTransportContext> TestSessionPtr;

class TestWebsocketServer
{
public:
    TestWebsocketServer(string listen_address, int listen_port) :
        m_server(IoContextPool::get_instance(), listen_address, listen_port)
    {
        auto &endpoint1 = m_server.endpoint["^/([0-9a-z]{8}-[0-9a-z]{4}-[0-9a-z]{4}-[0-9a-z]{4}-[0-9a-z]{12})$"];
        endpoint1.on_open = [this](WsTransportContext& cxt) {
            const string& id = cxt.path_params[1];
            LogDebug << id << " open connection";
            //cxt.close(); 测试在on_open中关闭
            //return;
            {
                std::lock_guard<boost::fibers::mutex> lk(m_sessions_mutex);
                auto it = m_sessions.find(id);
                if (it == m_sessions.end())
                {
                    TestSessionPtr session = std::make_shared<WsTransportContext>();
                    *session = cxt;
                    session->recv = [](string content) -> string {
                        LogDebug << "recv body:" << content;
                        return content;
                    };
                    cxt.recv = session->recv;
                    m_sessions[id] = session;
                }
                else
                {
                    TestSessionPtr session = it->second;
                    *session = cxt;
                    session->recv = [](string content) -> string {
                        LogDebug << "recv body:" << content;
                        return content;
                    };
                    cxt.recv = session->recv;
                }
            }
        };

        endpoint1.on_close = [this](WsTransportContext& cxt) {
            const string& id = cxt.path_params[1];
            LogDebug << id << " closed connection";
            std::lock_guard<boost::fibers::mutex> lk(m_sessions_mutex);
            m_sessions.erase(id);
        };

        endpoint1.on_error = [this](WsTransportContext& cxt) {
            const string& id = cxt.path_params[1];
            LogWarn << id << " error on connection";
            std::lock_guard<boost::fibers::mutex> lk(m_sessions_mutex);
            m_sessions.erase(id);
        };

        boost::fibers::fiber([this](){
            while(1)
            {
                boost::this_fiber::sleep_for(std::chrono::seconds(3));
                {
                    std::lock_guard<boost::fibers::mutex> lk(m_sessions_mutex);
                    LogDebug << "current session num:" << m_sessions.size();
                }
            }
        }).detach();

    }

    ~TestWebsocketServer() = default;

    void start()
    {
        m_server.start();
    }



private:
    static TestWebsocketServer *m_instance;
    WsTransportServer m_server;
    map<string, TestSessionPtr> m_sessions;
    boost::fibers::mutex m_sessions_mutex;
};

#endif
