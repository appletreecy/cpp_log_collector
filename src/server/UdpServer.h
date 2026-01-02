#pragma once
#include <netinet/in.h>
#include <string>

#include "common/BlockingQueue.h"
#include "common/Stats.h"


class UdpServer{
public:
    UdpServer(int port, BlockingQueue<std::string>& q, Stats& stats);
    void run();


private:
    int sockfd;
    sockaddr_in addr{};
    BlockingQueue<std::string>& q_;
    Stats& stats_;
};