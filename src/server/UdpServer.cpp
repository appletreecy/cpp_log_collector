#include "UdpServer.h"
#include "common/Signal.h"
#include <unistd.h>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <cerrno>

UdpServer::UdpServer(int port, std::string outPath) : sockfd(-1), sink_(outPath){
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) throw std::runtime_error("socket() failed");

    std::memset(&addr, 0, sizeof(addr));

    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(sockfd, (sockaddr*)&addr, sizeof(addr)) < 0) {
        close(sockfd);
        throw std::runtime_error("bind() failed");
    }
}

void UdpServer::run(){
    char buffer[2048];

    while(!Signal::stopRequested()){
        ssize_t len = recvfrom(sockfd, buffer, sizeof(buffer)-1, 0, nullptr, nullptr);
        if (len < 0) {
            if (errno == EINTR){
                // Interrupted by SIGTERM/SIGINT — loop will exit if stop requested
                continue;
            }
            // Other recvfrom errors: keep going (or log later)
            continue;
        }

        if (len == 0) continue;

        buffer[len] = '\0';
        sink_.writeLine(buffer);
    }
    sink_.writeLine("Shutting down (signal received)...");
    close(sockfd);
}