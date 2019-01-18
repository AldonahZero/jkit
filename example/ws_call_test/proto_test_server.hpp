#ifndef TEST_API_H
#define TEST_API_H

#include "google/protobuf/message.h"
#include "addressbook.pb.h"
#include "msg_encode_decode.hpp"
#include "ws_call_session.h"

typedef std::shared_ptr<google::protobuf::Message> MessagePtr;

class TestServer;

class TestSession : public WsCallSession
{
public:
    TestSession(WsSessionContext& cxt, tcp::socket &s, TestServer& svr);

    virtual ~TestSession();

    MessagePtr send_msg_call(MessagePtr req);

    void test_async_send_call();
    void test_send_call();

private:
    MessagePtr on_recv_msg_call(MessagePtr req);

protected:
    virtual bool is_allow(const WsSessionContext& cxt);

    virtual CaseInsensitiveMultimap add_res_header(const WsSessionContext& cxt);

    virtual void on_close(const WsSessionContext& cxt);

    virtual void on_error(const WsSessionContext& cxt);

    virtual string on_recv_call(string req_body);

    virtual string on_recv_busy(string req_body);

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
            auto ses = WsSessionPtr(new TestSession(cxt, s, *this));
            return ses;
        };
    }

    void add_session(WsSessionPtr ses)
    {
        std::lock_guard<boost::fibers::mutex> lk(m_sessions_mutex);
        m_sessions.push_back(ses);
    }

    void remove_session(WsSessionPtr ses)
    {
        std::lock_guard<boost::fibers::mutex> lk(m_sessions_mutex);
        auto it = m_sessions.begin();
        for(; it!=m_sessions.end(); ++it)
        {
            if((*it).get() == ses.get())
            {
                m_sessions.erase(it);
                break;
            }
        }
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
        for(auto& ses : m_sessions)
        {
            ses->stop();
        }
        m_sessions.clear();
        m_server.stop();
        m_running = false;
        if(m_f.joinable())
        {
            m_f.join();
        }
    }


private:
    WsServer m_server;
    vector<WsSessionPtr> m_sessions;
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

MessagePtr TestSession::send_msg_call(MessagePtr req)
{
    string res = send_call(ProtobufMsgCodec::encode(*req));
    return MessagePtr(ProtobufMsgCodec::decode(res));
}

void TestSession::test_async_send_call()
{
    //测试异步
    auto self = shared_from_this();
    boost::fibers::fiber([self, this](){
        try
        {
            boost::this_fiber::sleep_for(std::chrono::seconds(1));
            AddressBook* req = new AddressBook;
            Person* p = req->add_people();
            p->set_name("this is a server request");

            LogDebug << "send req:" << req->GetTypeName() << "," << req->DebugString();
            MessagePtr res = send_msg_call(MessagePtr(req));
            LogDebug << "recv res:" << res->GetTypeName() << "," << res->DebugString();
        }
        catch(std::exception& e)
        {
            LogWarnExt << e.what();
        }

    }).detach();
}

void TestSession::test_send_call()
{
    try
    {
        AddressBook* req = new AddressBook;
        Person* p = req->add_people();
        p->set_name("this is a server request");

        LogDebug << "send req:" << req->GetTypeName() << "," << req->DebugString();
        MessagePtr res = send_msg_call(MessagePtr(req));
        LogDebug << "recv res:" << res->GetTypeName() << ","  << res->DebugString();
    }
    catch(std::exception& e)
    {
        LogErrorExt << e.what();
    }
}

MessagePtr TestSession::on_recv_msg_call(MessagePtr req)
{
    LogDebug << "recv req:" << req->GetTypeName() << "," << req->DebugString();
    test_async_send_call();
    //test_send_call();

    AddressBook* res = new AddressBook;
    Person* p = res->add_people();
    p->set_name("this is a server response");
    LogDebug << "send res:" << res->GetTypeName() << "," << res->DebugString();
    return MessagePtr(res);
}

bool TestSession::is_allow(const WsSessionContext& cxt)
{
    LogDebug << cxt.path;
    auto self = shared_from_this();
    m_svr.add_session(self);
    return true;
}

CaseInsensitiveMultimap TestSession::add_res_header(const WsSessionContext& cxt)
{
    return CaseInsensitiveMultimap();
}

void TestSession::on_close(const WsSessionContext& cxt)
{
    LogDebug << "client closed";
    auto self = shared_from_this();
    m_svr.remove_session(self);
}

void TestSession::on_error(const WsSessionContext& cxt)
{
    auto self = shared_from_this();
    m_svr.remove_session(self);
}

string TestSession::on_recv_call(string req_body)
{
    MessagePtr req(ProtobufMsgCodec::decode(req_body));
    if(!req)
    {
        LogErrorExt << "deocde protobuf msg failed";
        AddressBook* res = new AddressBook;
        Person* p = res->add_people();
        p->set_name("not support");
        MessagePtr res_ptr(res);
        return ProtobufMsgCodec::encode(*res_ptr);
    }

    MessagePtr res = on_recv_msg_call(req);
    return ProtobufMsgCodec::encode(*res);
}

string TestSession::on_recv_busy(string req_body)
{
    return "call busy";
}


#endif
