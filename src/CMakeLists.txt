project(klogger)
cmake_minimum_required(VERSION 2.8)

set(OUTDIR ${CMAKE_CURRENT_SOURCE_DIR}/../lib)
set(Boost_USE_STATIC_LIBS ON)
set(Boost_USE_MULTITHREADED ON)
#set(Boost_USE_STATIC_RUNTIME OFF)
#set(Boost_USE_STATIC_RUNTIME OFF)
#set(Boost_USE_STATIC_RUNTIME OFF)
#set(Boost_USE_STATIC_RUNTIME OFF)

include_directories(
 ${CMAKE_CURRENT_SOURCE_DIR}/../include
 )
set(LIBRARY_OUTPUT_PATH ${OUTDIR})

#生成共享库
#ADD_LIBRARY(${PROJECT_NAME} SHARED ${SRC_SRC})
#生成可执行文件
#add_executable(${PROJECT_NAME} ${SRC_LIST})

#链接所需要的库
#target_link_libraries(${PROJECT_NAME}  ${Boost_LIBRARIES} z)


#生成同步klogger静态库
ADD_LIBRARY(klogger_sync STATIC logger.cpp)
set_target_properties(klogger_sync PROPERTIES LINKER_LANGUAGE CXX)

#生成异步klogger静态库
ADD_LIBRARY(klogger_async STATIC logger.cpp)
target_compile_definitions(klogger_async PRIVATE KLOGGER_ASYNC=1)
set_target_properties(klogger_async PROPERTIES LINKER_LANGUAGE CXX)

#生成multi_client_http静态库
ADD_LIBRARY(multi_client_http STATIC multi_client_http.cpp)
set_target_properties(multi_client_http PROPERTIES LINKER_LANGUAGE CXX)

#生成request_api_server静态库
ADD_LIBRARY(request_api_server STATIC request_api_server.cpp)
set_target_properties(request_api_server PROPERTIES LINKER_LANGUAGE CXX)

#生成ws_call_server静态库
ADD_LIBRARY(ws_call_server STATIC ws_server.cpp ws_call_session.cpp)
set_target_properties(ws_call_server PROPERTIES LINKER_LANGUAGE CXX)

#生成ws_stream_server静态库
ADD_LIBRARY(ws_stream_server STATIC ws_server.cpp ws_stream_session.cpp)
set_target_properties(ws_stream_server PROPERTIES LINKER_LANGUAGE CXX)

#
#MESSAGE(STATUS "src files  is " ${SRC_LIST})
#MESSAGE(STATUS "boost include dir  is " ${Boost_INCLUDE_DIRS})
#MESSAGE(STATUS "boost libs dir  is " ${Boost_LIBRARY_DIR})
#MESSAGE(STATUS "boost libs is " ${Boost_LIBRARIES})
#MESSAGE(STATUS "project name is " ${PROJECT_NAME})
#

#list include dir
#get_property(dirs DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY INCLUDE_DIRECTORIES)
#foreach(dir ${dirs})
#  message(STATUS "include dir='${dir}'")
#endforeach()

SET(CMAKE_CXX_FLAGS ${CMAKE_CXX_FLAGS_RELWITHDEBINFO})
SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fPIC -std=c++1z")
MESSAGE(STATUS "cxx flags is ${CMAKE_CXX_FLAGS}")



