#pragma once
#include <netinet/in.h>
#include <string>
#include <cstdint>

#include "common/BlockingQueue.h"


class UdpServer{
public:
    UdpServer(int port, BlockingQueue<std::string>& q);
    void run();

    std::uint64_t dropped() const {return dropped_;}

private:
    int sockfd;
    sockaddr_in addr{};
    BlockingQueue<std::string>& q_;
    std::uint64_t dropped_{0};
};