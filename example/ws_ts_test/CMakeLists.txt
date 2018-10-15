project(test_api)
cmake_minimum_required(VERSION 2.8)
set(OUTDIR ../bin)

#set(Boost_USE_STATIC_LIBS ON)
#set(Boost_USE_MULTITHREADED ON)
#set(CMAKE_BUILD_TYPE DEBUG)

set(Boost_USE_STATIC_LIBS ON)
set(Boost_USE_MULTITHREADED ON)

set(Boost_LIBRARIES boost_log boost_fiber boost_context boost_log_setup boost_thread boost_filesystem boost_system boost_program_options boost_regex boost_date_time boost_chrono boost_locale)

include_directories(../../include)
link_directories(${CMAKE_CURRENT_SOURCE_DIR}/../../lib)
SET(EXECUTABLE_OUTPUT_PATH ${OUTDIR})
set(KLink_LIBRARIES klogger_sync backtrace pthread dl crypto ssl)
SET(CMAKE_CXX_FLAGS ${CMAKE_CXX_FLAGS_RELWITHDEBINFO})
SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-deprecated-declarations")
MESSAGE(STATUS "cxx flags is ${CMAKE_CXX_FLAGS}")


add_executable(stream_server server_main.cpp)
target_link_libraries(stream_server -Wl,--start-group ws_stream_server ${Boost_LIBRARIES} ${KLink_LIBRARIES} -Wl,--end-group)


add_executable(stream_client client_main.cpp)
target_link_libraries(stream_client -Wl,--start-group ${Boost_LIBRARIES} ${KLink_LIBRARIES} boost_coroutine -Wl,--end-group)



