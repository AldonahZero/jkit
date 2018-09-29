#include "test_websocket_server.hpp"

int main()
{
    IoContextPool::m_pool_size = 8;
    IoContextPool& pool = IoContextPool::get_instance();

    init_logging("./test_api.log", boost::log::trivial::debug);

    TestWebsocketServer server("0.0.0.0", 38080);
    server.start();

    pool.run();
}
