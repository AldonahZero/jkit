#include "test_server.hpp"
#include "fiber_frame_context.hpp"

int main()
{
    FiberFrameContext& frame_cxt = FiberFrameContext::instance();
    frame_cxt.run_thread_count = 3;
    frame_cxt.init();

    init_logging("./call_server.log", boost::log::trivial::debug);
    TestServer server("0.0.0.0", 38080);
    server.start();

    //模拟停止
//    boost::fibers::fiber([&server, &frame_cxt](){
//        boost::this_fiber::sleep_for(std::chrono::seconds(15));
//        server.stop();
//        std::cout << "server stop()\n";

//        frame_cxt.notify_stop();
//    }).detach();

    frame_cxt.wait();
    return 0;
}
