#pragma once
#include <atomic>
#include <cstdint>
#include <string>
#include <thread>

#include "common/Stats.h"
#include "common/BlockingQueue.h"

class MetricsServer {
public:
    MetricsServer(Stats& stats,
                  BlockingQueue<std::string>& queue,
                  std::string bindIp = "127.0.0.1",
                  int port = 9100);

    void start();
    void stop();

private:
    void run();
    static std::string buildHealthJson(const Stats& s, std::size_t qsize);
    static std::string buildPrometheus(const Stats& s, std::size_t qsize);
    static void sendAll(int fd, const char* data, std::size_t len);

private:
    Stats& stats_;
    BlockingQueue<std::string>& queue_;
    std::string bindIp_;
    int port_{9100};

    int listenFd_{-1};
    std::thread th_;
    std::atomic_bool running_{false};
};
