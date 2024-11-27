cmake_minimum_required(VERSION 3.11.0)

project(chat)

find_package(Protobuf REQUIRED)
include_directories(${Protobuf_INCLUDE_DIRS})
protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS message.proto)

include_directories("/usr/local/include" ${CMAKE_CURRENT_BINARY_DIR})
add_definitions(-std=c++14 -Wno-deprecated-declarations)

set(server_sources server.cpp ${PROTO_SRCS} ${PROTO_HDRS})
set(client_sources client.cpp ${PROTO_SRCS} ${PROTO_HDRS})

add_executable(server ${server_sources})
target_link_libraries(server ${Protobuf_LIBRARIES})

add_executable(client ${client_sources})
target_link_libraries(client ${Protobuf_LIBRARIES})
