#include "metrics/MetricsServer.h"
#include "common/Signal.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <poll.h>

#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <string>
#include <string_view>

static int makeListenSocket(const std::string& ip, int port) {
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) throw std::runtime_error("metrics socket() failed");

    int yes = 1;
    (void)::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port));
    if (::inet_pton(AF_INET, ip.c_str(), &addr.sin_addr) != 1) {
        ::close(fd);
        throw std::runtime_error("metrics inet_pton() failed");
    }

    if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        ::close(fd);
        throw std::runtime_error("metrics bind() failed (is port in use?)");
    }

    if (::listen(fd, 64) != 0) {
        ::close(fd);
        throw std::runtime_error("metrics listen() failed");
    }

    return fd;
}

static std::string_view firstLine(std::string_view s) {
    auto pos = s.find("\r\n");
    if (pos == std::string_view::npos) pos = s.find('\n');
    return (pos == std::string_view::npos) ? s : s.substr(0, pos);
}

static void readHttpRequestLine(int fd, std::string& out) {
    out.clear();
    char buf[1024];
    // Read a small amount; enough for request line + headers for tiny clients
    ssize_t n = ::recv(fd, buf, sizeof(buf), 0);
    if (n > 0) out.assign(buf, buf + n);
}

MetricsServer::MetricsServer(Stats& stats,
                             BlockingQueue<std::string>& queue,
                             std::string bindIp,
                             int port)
    : stats_(stats), queue_(queue), bindIp_(std::move(bindIp)), port_(port) {}

void MetricsServer::start() {
    running_.store(true, std::memory_order_relaxed);
    th_ = std::thread(&MetricsServer::run, this);
}

void MetricsServer::stop() {
    running_.store(false, std::memory_order_relaxed);
    // If blocked in poll, the Signal self-pipe will wake it on SIGTERM.
    // If you want manual stop without signal, you can also close listenFd_ here:
    if (listenFd_ != -1) {
        ::shutdown(listenFd_, SHUT_RDWR);
        ::close(listenFd_);
        listenFd_ = -1;
    }
    if (th_.joinable()) th_.join();
}

void MetricsServer::sendAll(int fd, const char* data, std::size_t len) {
    std::size_t sent = 0;
    while (sent < len) {
        ssize_t n = ::send(fd, data + sent, len - sent, 0);
        if (n <= 0) return;
        sent += static_cast<std::size_t>(n);
    }
}

std::string MetricsServer::buildHealthJson(const Stats& s, std::size_t qsize) {
    auto recv = s.received.load(std::memory_order_relaxed);
    auto drop = s.dropped.load(std::memory_order_relaxed);
    auto written = s.written.load(std::memory_order_relaxed);

    std::string body;
    body += "{";
    body += "\"status\":\"ok\",";
    body += "\"received\":" + std::to_string(recv) + ",";
    body += "\"written\":" + std::to_string(written) + ",";
    body += "\"dropped\":" + std::to_string(drop) + ",";
    body += "\"queue_depth\":" + std::to_string(qsize);
    body += "}";
    return body;
}

std::string MetricsServer::buildPrometheus(const Stats& s, std::size_t qsize) {
    auto recv = s.received.load(std::memory_order_relaxed);
    auto drop = s.dropped.load(std::memory_order_relaxed);
    auto written = s.written.load(std::memory_order_relaxed);

    std::string m;
    m += "# HELP logcollector_received_total Total UDP packets received\n";
    m += "# TYPE logcollector_received_total counter\n";
    m += "logcollector_received_total " + std::to_string(recv) + "\n";

    m += "# HELP logcollector_written_total Total log lines written\n";
    m += "# TYPE logcollector_written_total counter\n";
    m += "logcollector_written_total " + std::to_string(written) + "\n";

    m += "# HELP logcollector_dropped_total Total UDP packets dropped due to full queue\n";
    m += "# TYPE logcollector_dropped_total counter\n";
    m += "logcollector_dropped_total " + std::to_string(drop) + "\n";

    m += "# HELP logcollector_queue_depth Current queue depth\n";
    m += "# TYPE logcollector_queue_depth gauge\n";
    m += "logcollector_queue_depth " + std::to_string(qsize) + "\n";

    return m;
}

void MetricsServer::run() {
    listenFd_ = makeListenSocket(bindIp_, port_);

    pollfd fds[2];
    fds[0].fd = listenFd_;
    fds[0].events = POLLIN;
    fds[1].fd = Signal::wakeFd();
    fds[1].events = POLLIN;

    while (running_.load(std::memory_order_relaxed) && !Signal::stopRequested()) {
        int rc = ::poll(fds, 2, -1);
        if (rc < 0) {
            if (errno == EINTR) continue;
            continue;
        }

        // Wake on signal (self-pipe)
        if (Signal::stopRequested() || (fds[1].revents & POLLIN)) {
            // Drain wake pipe
            uint8_t tmp[256];
            while (true) {
                ssize_t n = ::read(fds[1].fd, tmp, sizeof(tmp));
                if (n <= 0) break;
            }
            break;
        }

        if (fds[0].revents & POLLIN) {
            int cfd = ::accept(listenFd_, nullptr, nullptr);
            if (cfd < 0) continue;

            std::string req;
            readHttpRequestLine(cfd, req);

            const auto line = firstLine(req);
            // Very tiny HTTP parsing:
            // "GET /metrics HTTP/1.1"
            bool isMetrics = (line.find("GET /metrics") == 0);
            bool isHealth  = (line.find("GET /health") == 0);

            std::size_t qsize = queue_.size();

            std::string body;
            std::string contentType;

            if (isMetrics) {
                body = buildPrometheus(stats_, qsize);
                contentType = "text/plain; version=0.0.4";
            } else if (isHealth) {
                body = buildHealthJson(stats_, qsize);
                contentType = "application/json";
            } else {
                body = "Not Found\n";
                contentType = "text/plain";
                std::string resp =
                    "HTTP/1.1 404 Not Found\r\n"
                    "Content-Type: " + contentType + "\r\n"
                    "Content-Length: " + std::to_string(body.size()) + "\r\n"
                    "Connection: close\r\n\r\n" + body;
                sendAll(cfd, resp.data(), resp.size());
                ::close(cfd);
                continue;
            }

            std::string resp =
                "HTTP/1.1 200 OK\r\n"
                "Content-Type: " + contentType + "\r\n"
                "Content-Length: " + std::to_string(body.size()) + "\r\n"
                "Connection: close\r\n\r\n" + body;

            sendAll(cfd, resp.data(), resp.size());
            ::close(cfd);
        }
    }

    if (listenFd_ != -1) {
        ::close(listenFd_);
        listenFd_ = -1;
    }
}
