#include "server/UdpServer.h"
#include "common/Signal.h"

#include <unistd.h>
#include <poll.h>
#include <cstring>
#include <cerrno>
#include <cstdint>
#include <stdexcept>

UdpServer::UdpServer(int port, BlockingQueue<std::string>& q, Stats& stats)
    : sockfd(-1), q_(q), stats_(stats)
{
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
            continue;
        }

        // signal wakeup => drain & exit
        if (Signal::stopRequested() || (fds[1].revents & POLLIN)) {
            std::uint8_t tmp[256];
            while (true) {
                ssize_t n = read(fds[1].fd, tmp, sizeof(tmp));
                if (n <= 0) break;
            }
            close(sockfd);
            return;
        }

        if (fds[0].revents & POLLIN) {
            ssize_t len = recvfrom(sockfd, buffer, sizeof(buffer) - 1, 0, nullptr, nullptr);
            if (len <= 0) continue;

            buffer[len] = '\0';
            stats_.received.fetch_add(1, std::memory_order_relaxed);

            // enqueue (fast). If queue full, drop and count.
            if (!q_.tryPush(std::string(buffer))) {
                stats_.dropped.fetch_add(1, std::memory_order_relaxed);
            }
        }
    }
}
