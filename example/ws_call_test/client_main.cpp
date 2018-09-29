
#include "json.hpp"
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/http.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/spawn.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <string>

#include <boost/uuid/random_generator.hpp>
#include <boost/uuid/uuid_io.hpp>


using namespace std;
using tcp = boost::asio::ip::tcp;               // from <boost/asio/ip/tcp.hpp>
namespace websocket = boost::beast::websocket;  // from <boost/beast/websocket.hpp>
namespace http = boost::beast::http;    // from <boost/beast/http.hpp>
//------------------------------------------------------------------------------

// Report a failure
void
fail(boost::system::error_code ec, string what)
{
    std::cerr << what << ": " << ec.message() << "\n";
}

// Sends a WebSocket message and prints the response
void do_session(
        std::string const& host,
        std::string const& port,
        std::string const& target,
        std::string const& text,
        boost::asio::io_context& ios,
        boost::asio::yield_context yield)
{
    boost::system::error_code ec;

    tcp::resolver resolver{ios};
    websocket::stream<tcp::socket> ws{ios};

    //dns
    auto const lookup = resolver.async_resolve({host, port}, yield[ec]);
    if(ec)
        return fail(ec, "resolve, " + target);

    boost::asio::async_connect(ws.next_layer(), lookup, yield[ec]);
    if(ec)
        return fail(ec, "connect, " + target);

    // handsharke
    http::response<http::string_body> res;
    ws.async_handshake(res, host, target, yield[ec]);
    if(ec)
        return fail(ec, "handshake, " + target);

    //获取自定义的响应头
    auto it = res.find("test");
    if(it != res.end())
    {
        std::cout << "test" << ": " << (*it).value() << "\n";
    }

    int id_size = 4;
    int recv_req = 0;
    int send_req = 0;

    //发送id
    int send_id = 1;
    while(1)
    {
        //获取发送参数
        string type = "s";
        string req_id;
        int id = send_id;
        ++send_id;
        if(send_id > 9999)
        {
            send_id = 1;
        }
        char tmp_id[8];
        sprintf(tmp_id, "%04d", id);
        req_id = tmp_id;

        nlohmann::json body;
        body["client"] = "this is a client request";

        //发送数据
        string req;
        std::vector<boost::asio::const_buffer> buffers;
        req.append(type);
        req.append(req_id);
        req.append(body.dump());

        ws.async_write(boost::asio::buffer(req), yield[ec]);
        if(ec)
            return fail(ec, "write, " + target);


        //接收数据
        boost::beast::multi_buffer b;
        ws.async_read(b, yield[ec]);
        if(ec)
            return fail(ec, "read, " + target);

        string msg =  boost::beast::buffers_to_string(b.data());

        string msg_type = msg.substr(0, 1);
        if(msg_type == "s") //来自服务端的请求
        {
            std::cout << msg << std::endl;
            nlohmann::json tmp;
            tmp["client"] = "this is a client response";
            string res;
            res.append("r");
            res.append(msg.substr(1, id_size));
            res.append(tmp.dump());
            ws.async_write(boost::asio::buffer(res), yield[ec]);
            ++recv_req;
            if(ec)
                return fail(ec, "write, " + target);
        }
        else if(msg_type == "r") //响应
        {
            ++send_req;
            std::cout << msg << "------" <<  target << std::endl;
        }
        else
        {
            return fail(ec, "format error," + target);
        }

        if(send_req == 5) //发送5次后强制结束
        {
            break;
        }
    }

    ws.async_close(websocket::close_code::normal, yield[ec]);
    if(ec)
        return fail(ec, "close, " + target);

    // If we get here then the connection is closed gracefully

}

//------------------------------------------------------------------------------

int main(int argc, char** argv)
{
    if(argc != 5)
    {
        std::cerr <<
                     "Usage: " << argv[0] << " <host> <port> <text> <count>\n" <<
                     "Example:\n    " << argv[0] <<
                     " 127.0.0.1 38080  \"Hello, world!\" 30\n";
        return EXIT_FAILURE;
    }
    auto const host = argv[1];
    auto const port = argv[2];
    auto const text = argv[3];
    int count = static_cast<int>(std::atoi(argv[4]));

    // The io_service is required for all I/O
    boost::asio::io_context ios;

    for(int i=0; i<count; ++i)
    {
        string target = "/" + boost::uuids::to_string(boost::uuids::random_generator()());
        // Launch the asynchronous operation
        boost::asio::spawn(ios, std::bind(
                               &do_session,
                               std::string(host),
                               std::string(port),
                               std::string(target),
                               std::string(text),
                               std::ref(ios),
                               std::placeholders::_1));
    }

    ios.run();

    return EXIT_SUCCESS;
}
