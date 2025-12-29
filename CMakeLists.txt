cmake_minimum_required(VERSION 3.10)
project(log_collector)

set(CMAKE_CXX_STANDARD 17)

add_executable(log_collector
        src/main.cpp
        src/server/UdpServer.cpp
        src/sink/FileSink.cpp
        src/common/Signal.cpp
)

target_include_directories(log_collector PRIVATE src)