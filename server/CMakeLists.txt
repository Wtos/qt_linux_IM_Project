cmake_minimum_required(VERSION 3.10)
project(IMServer VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(SERVER_SOURCES
    src/main.cpp
    src/server.cpp
    src/client_manager.cpp
    src/protocol.cpp
    src/epoll_wrapper.cpp
    src/reactor.cpp
)

include_directories(${CMAKE_SOURCE_DIR}/src)
include_directories(${CMAKE_SOURCE_DIR}/include)

add_executable(im_server ${SERVER_SOURCES})

find_package(Threads REQUIRED)
target_link_libraries(im_server Threads::Threads)

target_compile_options(im_server PRIVATE
    -Wall
    -Wextra
    -O2
    -g
)

install(TARGETS im_server DESTINATION bin)

message(STATUS "IMServer configuration complete")
message(STATUS "C++ standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
