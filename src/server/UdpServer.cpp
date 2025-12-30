#include "UdpServer.h"
#include "common/Signal.h"
#include <unistd.h>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <cerrno>
#include <poll.h>

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

void UdpServer::run() {
    char buffer[2048];

    pollfd fds[2];
    fds[0].fd = sockfd;
    fds[0].events = POLLIN;
    fds[1].fd = Signal::wakeFd();
    fds[1].events = POLLIN;

    while (true) {
        int rc = poll(fds, 2, -1);
        if (rc < 0) {
            if (errno == EINTR) continue;
            continue; // could log error later
        }

        // If signal was requested OR wake pipe readable, exit immediately
        if (Signal::stopRequested() || (fds[1].revents & POLLIN)) {
            // Drain wake pipe (important so it doesn't keep triggering)
            uint8_t tmp[256];
            while (true) {
                ssize_t n = read(fds[1].fd, tmp, sizeof(tmp));
                if (n <= 0) break;
            }

            sink_.writeLine("Shutting down (self pipe wakeup)...");
            close(sockfd);
            return; // ✅ ensures we exit run() exactly once
        }

        if (fds[0].revents & POLLIN) {
            ssize_t len = recvfrom(sockfd, buffer, sizeof(buffer) - 1, 0, nullptr, nullptr);
            if (len > 0) {
                buffer[len] = '\0';
                sink_.writeLine(buffer);
            }
        }
    }
}