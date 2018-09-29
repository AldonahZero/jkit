服务端开发常用工具库,使用 gcc 7.2.1编译,包含以下库  
boost 1.68.0  
nghttp2 1.33.0  
soci 4.0.0 master  
libbacktrace 用于boost stacktrace  
jwt Json web token (JWT)  
nlohmann json  2.1.1  

exception_trace.hpp 封装了抛出带堆栈信息的函数  
multi_client_http.h 一个服务端专用的http客户端,支持http https http2，基于协程封装  
request_api_server.h 一个轻量基于协程的resetful 服务封装  
ws_call_session.h 一个基于websocket和协程的的双向rpc服务封装  
ws_stream_session.h 主要用来做流媒体传输，基于协程  
wait_stuff.hpp 多协程同时等待结果的一个封装  
fiber_frame_context.hpp fiber运行环境的一个封装  
logger.h boost.log的一个封装,支持syslog  