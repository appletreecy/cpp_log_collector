#pragma once
#include <netinet/in.h>
#include <string>
#include <sink/FileSink.h>


class UdpServer{
public:
    explicit UdpServer(int port, std::string outPath);
    void run();

private:
    int sockfd;
    sockaddr_in addr{};
    FileSink sink_;
};