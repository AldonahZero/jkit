#ifndef WS_CALL_SESSION_H
#define WS_CALL_SESSION_H

#include "ws_server.h"

class TransactionMsg;
typedef std::shared_ptr<TransactionMsg> TransactionMsgPtr;

//使用websocket协议, 用来处理双向请求响应

//底层消息格式
//第一个字节为 s 或 r, s为请求 r为响应
//后面为一个整数字符串,长度为4个字符 比如 "0001" "3321" "9999"
//最后是消息体
//使用者只需使用send传消息体, 消息类型和id由库自动生成处理

class WsCallSession : public WsSession
{
public:
    //最大支持8个在缓存队列中
    WsCallSession(WsSessionContext& cxt, tcp::socket &s, int recv_call_buffer = 8);
    virtual ~WsCallSession();

    void start() override;
    void stop() override;

    string send_call(string req_body);

protected:
    virtual bool is_allow(const WsSessionContext& cxt) {return true;}
    virtual CaseInsensitiveMultimap add_res_header(const WsSessionContext& cxt) {return CaseInsensitiveMultimap(); }
    virtual void on_close(const WsSessionContext& cxt) {}
    virtual void on_error(const WsSessionContext& cxt) {}

    //处理消息
    virtual string on_recv_call(string req_body) {return "not implemented";}

    //接收call缓存满时返回
    virtual string on_recv_busy(string req_body) {return "call busy"; }

private:
    bool process_handshake();
    void process_recv_call();
    void process_response(string id, string res_body);

    void process_send_data();

    void process_recv_data();
    void close();

    void send_recv_response(string id, string res_body);

protected:
    WsSessionContext m_cxt;
    int m_timeout_seconds = 10; //请求超时时间

private:
    websocket::stream<tcp::socket> m_ws;

    typedef boost::fibers::unbuffered_channel<string> send_channel_t;
    send_channel_t m_send_channel;

    //使用一个定长队列,以便不堵塞接收数据
    typedef boost::fibers::buffered_channel<string> recv_channel_t;
    recv_channel_t m_recv_channel;

    boost::fibers::mutex m_mutex;
    boost::unordered_map<string, TransactionMsgPtr> m_trans_msgs;

    uint32_t m_send_id = 1;

    boost::fibers::fiber m_send_fiber;
    boost::fibers::fiber m_recv_fiber;
    boost::fibers::fiber m_recv_call_fiber;

    bool m_running = false;
    boost::fibers::condition_variable_any m_cnd;
};

#endif // WS_CALL_SESSION_H
