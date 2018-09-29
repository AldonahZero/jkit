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
fail(boost::system::error_code ec, char const* what)
{
    std::cerr << what << ": " << ec.message() << "\n";
}



// Sends a WebSocket message and prints the response
void
do_session(
        std::string const& host,
        std::string const& port,
        std::string const& target,
        std::string const& text,
        boost::asio::io_context& ios,
        boost::asio::yield_context yield)
{
    boost::system::error_code ec;

    // These objects perform our I/O
    tcp::resolver resolver{ios};
    websocket::stream<tcp::socket> ws{ios};

    // Look up the domain name
    auto const lookup = resolver.async_resolve({host, port}, yield[ec]);
    if(ec)
        return fail(ec, "resolve");

    // Make the connection on the IP address we get from a lookup
    boost::asio::async_connect(ws.next_layer(), lookup, yield[ec]);
    if(ec)
        return fail(ec, "connect");

    // Perform the websocket handshake
    http::response<http::string_body> res;
    ws.async_handshake(res, host, target, yield[ec]);
    if(ec)
        return fail(ec, "handshake");

    auto it = res.find("test");
    if(it != res.end())
    {
        std::cout << "test" << ": " << (*it).value() << "\n";
    }

    string type = "s";
    string req_id = boost::uuids::to_string(boost::uuids::random_generator()());
    string body = "helloeeeeeeeeeeeeeeeeeeeeeeeeeeeeee";

    std::vector<boost::asio::const_buffer> buffers;
    buffers.push_back(boost::asio::buffer(type));
    buffers.push_back(boost::asio::buffer(req_id));
    buffers.push_back(boost::asio::buffer(body));

    // Send the message
    ws.async_write(buffers, yield[ec]);
    if(ec)
        return fail(ec, "write");

    // This buffer will hold the incoming message
    boost::beast::multi_buffer b;

    // Read a message into our buffer
    ws.async_read(b, yield[ec]);
    if(ec)
        return fail(ec, "read");

    // Close the WebSocket connection
    ws.async_close(websocket::close_code::normal, yield[ec]);
    if(ec)
        return fail(ec, "close");

    // If we get here then the connection is closed gracefully

    // The buffers() function helps print a ConstBufferSequence
    std::cout << boost::beast::buffers(b.data()) << std::endl;
}

//------------------------------------------------------------------------------

int main(int argc, char** argv)
{
    // Check command line arguments.
    if(argc != 5)
    {
        std::cerr <<
                     "Usage: websocket-client <host> <port> <text>\n" <<
                     "Example:\n" <<
                     "    websocket-client 127.0.0.1 38080  \"Hello, world!\" count\n";
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

    // Run the I/O service. The call will return when
    // the get operation is complete.
    ios.run();

    return EXIT_SUCCESS;
}
