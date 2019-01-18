#include "ws_call_session.h"

struct TransactionMsg
{
    //boost::posix_time::ptime send_time;
    boost::fibers::promise<string> promise;
};

WsCallSession::WsCallSession(WsSessionContext& cxt, tcp::socket& s, int recv_call_buffer) :
    WsSession(cxt, s),
    m_cxt(std::move(cxt)),
    m_ws(std::move(s)),
    m_recv_channel(recv_call_buffer)
{

}

WsCallSession::~WsCallSession()
{

}

void WsCallSession::start()
{
    if(!process_handshake())
    {
        return;
    }

    {
        std::lock_guard<boost::fibers::mutex> lk(m_mutex);
        m_running = true;
    }
    m_ws.binary(true);

    m_send_id = 1;

    boost::fibers::fiber fsend([this](){
        this->process_send_data();
    });

    m_send_fiber.swap(fsend);

    boost::fibers::fiber frecv_call([this](){
        this->process_recv_call();
    });

    m_recv_call_fiber.swap(frecv_call);

    boost::fibers::fiber frecv([this](){
        this->process_recv_data();
    });
    m_recv_fiber.swap(frecv);
    m_recv_fiber.join();


    {   //结束时,让事务消息不再堵塞
        std::lock_guard<boost::fibers::mutex> lk(m_mutex);
        for(auto& p : m_trans_msgs)
        {
            p.second->promise.set_exception(make_with_trace_code(std::runtime_error("network failed"), static_cast<int>(WsCallError::NETWORK_ERROR)));
        }
        m_trans_msgs.clear();
    }

    close();

    {
        std::lock_guard<boost::fibers::mutex> lk(m_mutex);
        m_running = false;
        m_cnd.notify_all();
    }
}

void WsCallSession::stop()
{
    boost::system::error_code ec;
    m_ws.next_layer().shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);
    {
        std::unique_lock<boost::fibers::mutex> lk(m_mutex);;
        m_cnd.wait_for(lk, std::chrono::seconds(5), [this](){
            return !m_running;
        });
    }
}

void WsCallSession::close()
{
    if(!m_send_channel.is_closed())
    {
        //LogDebug << "----------m_send_channel.close()";
        m_send_channel.close();
    }
    if(!m_recv_channel.is_closed())
    {
        //LogDebug << "----------m_recv_channel.close()";
        m_recv_channel.close();
    }

    if(m_send_fiber.joinable())
    {
        //LogDebug << "----------start m_send_fiber.join()";
        m_send_fiber.join();
        //LogDebug << "----------stop m_send_fiber.join()";
    }
    if(m_recv_call_fiber.joinable())
    {
        //LogDebug << "----------start m_recv_call_fiber.join()";
        m_recv_call_fiber.join();
        //LogDebug << "----------stop m_recv_call_fiber.join()";
    }
}

void WsCallSession::process_recv_data()
{
    string recv_buf;
    char type;
    string id;
    int id_size = 4;
    string body;
    boost::fibers::future<boost::system::error_code> f;
    boost::system::error_code ec;

    for(;;)
    {
        boost::beast::flat_buffer buffer;
        f = m_ws.async_read(buffer, boost::asio::fibers::use_future([](boost::system::error_code ec, size_t n) {
                                return ec;
                            }));
        ec = f.get();
        if(ec == boost::beast::websocket::error::closed || ec == boost::asio::error::eof)
        {
            m_ws.next_layer().shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);
            on_close(m_cxt);
            break;
        }

        if(ec)
        {
            LogErrorExt << ec.message() << "\nremote_ip:" << m_cxt.remote_endpoint;
            m_ws.next_layer().shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);
            on_error(m_cxt);
            break;
        }

        recv_buf = boost::beast::buffers_to_string(buffer.data());
        if(recv_buf.size() <= (id_size + 1))
        {
            LogErrorExt << "msg format error" << "\nremote_ip:" << m_cxt.remote_endpoint;
            m_ws.next_layer().shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);
            on_error(m_cxt);
            break;
        }
        type = recv_buf[0];
        if(type == 's') //请求
        {
            auto s = m_recv_channel.try_push(std::move(recv_buf));
            if(s != boost::fibers::channel_op_status::success)
            {
                if (s == boost::fibers::channel_op_status::closed)
                {
                    LogDebugExt << "push failed, status:" << static_cast<int>(s);
                }
                else if(s == boost::fibers::channel_op_status::full)
                {
                    //太频繁,返回一个服务器忙
                    send_recv_response(recv_buf.substr(1, id_size), on_recv_busy(recv_buf.substr(id_size + 1)));
                }
                else
                {
                    LogErrorExt << "push failed, status:" << static_cast<int>(s);
                }
            }
        }
        else if(type == 'r') //响应
        {
            id = recv_buf.substr(1, id_size);
            body = recv_buf.substr(id_size + 1);
            process_response(std::move(id), std::move(body));
        }
        else //非法数据
        {
            LogErrorExt << "incorrect data" << "\nremote_ip:" << m_cxt.remote_endpoint;
            m_ws.next_layer().shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);
            on_error(m_cxt);
            break;
        }
    }
}

bool WsCallSession::process_handshake()
{
    boost::system::error_code ec;
    if(!is_allow(m_cxt))
    {
        m_ws.next_layer().shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);
        return false;
    }

    boost::fibers::future<boost::system::error_code> f;
    CaseInsensitiveMultimap custom_header = add_res_header(m_cxt);

    auto res_decorator = [&custom_header](http::response<http::string_body>& upgrade_response) {
        for(auto& p : custom_header)
        {
            upgrade_response.insert(p.first, p.second);
        }
    };

    f = m_ws.async_accept_ex(m_cxt.req, res_decorator, boost::asio::fibers::use_future([](boost::system::error_code ec) {
                                 return ec;
                             }));
    ec = f.get();
    if(ec)
    {
        LogErrorExt << ec.message() << "\nremote_ip:" << m_cxt.remote_endpoint;
        on_error(m_cxt);
        return false;
    }
    return true;
}

void WsCallSession::process_recv_call()
{
    string id;
    string req_body;
    string data;
    int id_size = 4;
    string res_body;
    while(1)
    {
        //LogDebug << "-------------start m_recv_channel.pop()";
        boost::fibers::channel_op_status s = m_recv_channel.pop(data);
        //LogDebug << "-------------stop m_recv_channel.pop()";
        if(s != boost::fibers::channel_op_status::success)
        {
            if(s != boost::fibers::channel_op_status::closed)
            {
                LogErrorExt << "recv pop failed, status:" << static_cast<int>(s);
            }
            else
            {
                LogDebugExt << "recv pop failed, status:" << static_cast<int>(s);
            }
            break;
        }

        id = data.substr(1, id_size);
        req_body = data.substr(id_size + 1);

        try
        {
            //返回响应
            res_body = on_recv_call(std::move(req_body));
        }
        catch(std::exception& e)
        {
            LogErrorExt << e.what() << "," << typeid(e).name();
            res_body = e.what();
        }
        send_recv_response(std::move(id), std::move(res_body));
    }
}

void WsCallSession::send_recv_response(string id, string res_body)
{
    string res;
    res.append("r");
    res.append(id);
    res.append(res_body);

    //发送响应,网络处理结果只是打印错误日志
    //LogDebug << "-------------start m_send_channel.push";
    boost::fibers::channel_op_status s = m_send_channel.push(std::move(res));
    //LogDebug << "-------------stop m_send_channel.push";
    if(s != boost::fibers::channel_op_status::success)
    {
        if(s != boost::fibers::channel_op_status::closed)
        {
            LogErrorExt << "send channel failed, status:" << static_cast<int>(s);
        }
        else
        {
            LogDebugExt << "send channel failed, status:" << static_cast<int>(s);
        }
    }
}

void WsCallSession::process_response(string id, string res_body)
{
    std::lock_guard<boost::fibers::mutex> lk(m_mutex);
    auto msg_it = m_trans_msgs.find(id);
    if (msg_it != m_trans_msgs.end())
    {
        msg_it->second->promise.set_value(res_body);
        m_trans_msgs.erase(msg_it);
    }
    else //已超时
    {
        LogErrorExt << "can not find transtion msg, already timeout, id:" << id;
    }
}

string WsCallSession::send_call(string req_body)
{
    boost::fibers::promise<string> promise;
    boost::fibers::future<string> future(promise.get_future());
    int32_t id;
    {
        std::lock_guard<boost::fibers::mutex> lk(m_mutex);
        id = m_send_id;
        ++m_send_id;
        if(m_send_id > 9999)
        {
            m_send_id = 1;
        }
    }

    char tmp_id[8];
    sprintf(tmp_id, "%04d", id);
    string str_id(tmp_id);

    string req;
    req.append("s");//s表示请求 r表示回复
    req.append(str_id);
    req.append(req_body);

    TransactionMsgPtr tmsg(new TransactionMsg());
    //tmsg->send_time = boost::posix_time::second_clock::local_time();
    tmsg->promise = std::move(promise);

    {
        std::lock_guard<boost::fibers::mutex> lk(m_mutex);
        m_trans_msgs[str_id] = tmsg;
    }

    //LogDebug << "-------------start m_send_channel.push1";
    boost::fibers::channel_op_status s = m_send_channel.push(std::move(req));
    //LogDebug << "-------------stop m_send_channel.push1";
    if(s != boost::fibers::channel_op_status::success)
    {
        if(s != boost::fibers::channel_op_status::closed)
        {
            LogErrorExt << "send channel failed, status:" << static_cast<int>(s);
        }
        else
        {
            LogDebugExt << "send channel failed, status:" << static_cast<int>(s);
        }

        {
            std::lock_guard<boost::fibers::mutex> lk(m_mutex);
            auto it = m_trans_msgs.find(str_id);
            if(it != m_trans_msgs.end())
            {
                it->second->promise.set_exception(make_with_trace_code(std::runtime_error("send channel failed"), static_cast<int>(WsCallError::NETWORK_ERROR)));
                m_trans_msgs.erase(it);
            }
        }
        return future.get();
    }

    //等待响应的超时时间
    boost::fibers::future_status fs = future.wait_for(std::chrono::seconds(m_timeout_seconds));
    if (fs == boost::fibers::future_status::timeout)
    {
        {
            std::lock_guard<boost::fibers::mutex> lk(m_mutex);
            auto it = m_trans_msgs.find(str_id);
            if(it != m_trans_msgs.end())
            {
                it->second->promise.set_exception(make_with_trace_code(std::runtime_error("timeout"), static_cast<int>(WsCallError::TIMEOUT)));
                m_trans_msgs.erase(it);
            }
        }
    }

    return future.get();
}

void WsCallSession::process_send_data()
{
    boost::fibers::future<boost::system::error_code> f;
    boost::system::error_code ec;
    string data;
    while(1)
    {
        //boost 1.67 pop有可能会抛出Operation not permitted异常的bug,需要更新到 1.68
        //LogDebug << "-------------start m_send_channel.pop";
        boost::fibers::channel_op_status s = m_send_channel.pop(data);
        //LogDebug << "-------------stop m_send_channel.pop";
        if(s != boost::fibers::channel_op_status::success)
        {
            if(s != boost::fibers::channel_op_status::closed)
            {
                LogErrorExt << "send pop failed, status:" << static_cast<int>(s);
            }
            else
            {
                LogDebugExt << "send pop failed, status:" << static_cast<int>(s);
            }
            break;
        }
        f = m_ws.async_write(boost::asio::buffer(data), boost::asio::fibers::use_future([](boost::system::error_code ec, size_t n){
                                 return ec;
                             }));
        ec = f.get();
        if(ec)
        {
            LogErrorExt << ec.message();
            //不能在此处break, bug 在一个协程push还没返回时,另一协程调用close, 如果没有调用pop,会导致push永远堵塞
            //break;
        }
    }
}
