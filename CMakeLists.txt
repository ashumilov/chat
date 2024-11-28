cmake_minimum_required(VERSION 3.11.0)

project(chat)

find_package(Protobuf REQUIRED)
include_directories(${Protobuf_INCLUDE_DIRS})
protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS src/message.proto)

add_subdirectory(src/spdlog)

include_directories(${CMAKE_CURRENT_BINARY_DIR})
add_definitions(-std=c++17 -Wno-deprecated-declarations)

set(server_sources src/server.cpp ${PROTO_SRCS} ${PROTO_HDRS})
set(client_sources src/client.cpp ${PROTO_SRCS} ${PROTO_HDRS})

add_executable(server ${server_sources})
target_link_libraries(server ${Protobuf_LIBRARIES} spdlog::spdlog)

add_executable(client ${client_sources})
target_link_libraries(client ${Protobuf_LIBRARIES} spdlog::spdlog)
