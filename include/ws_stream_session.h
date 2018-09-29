#ifndef WS_SESSION_SESSION_H
#define WS_SESSION_SESSION_H

#include "ws_server.h"
#include <queue>

//使用websocket协议, 用来处理流数据
class WsStreamSession : public WsSession
{
public:
    WsStreamSession(WsSessionContext& cxt, tcp::socket &s);
    virtual ~WsStreamSession();

    void start() override;
    void stop();

    //不使用string而使用any作接口,是因为on_send_block中处理发送缓冲需要额外信息,比如时间
    void send(boost::any pkt);

protected:
    virtual bool is_allow(const WsSessionContext& cxt) {return true;}
    virtual CaseInsensitiveMultimap add_res_header(const WsSessionContext& cxt) {return CaseInsensitiveMultimap(); }
    virtual void on_close(const WsSessionContext& cxt) {}
    virtual void on_error(const WsSessionContext& cxt) {}

    virtual const string &get_send_data(const boost::any* pkt);

    //数据发送完
    virtual void on_send_empty();

    //数据发送被堵塞
    virtual void on_send_block() = 0;

    //接收到数据包
    virtual void on_recv_data(string data) {}

private:
    bool process_handshake();
    void process_send_data();

    void process_recv_data();
    void close();

    void set_socket_opt();

protected:
    WsSessionContext m_cxt;
    boost::fibers::mutex m_send_mutex;
    std::queue<boost::any> m_send_buffer;

private:
    websocket::stream<tcp::socket> m_ws;
    boost::fibers::mutex m_mutex;
    boost::fibers::fiber m_send_fiber;
    boost::fibers::fiber m_recv_fiber;

    bool m_running = false;
    boost::fibers::condition_variable_any m_cnd;
};

#endif // WS_CALL_SESSION_H
