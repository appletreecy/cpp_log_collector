#pragma once
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <thread>

#include "common/BlockingQueue.h"
#include "common/Stats.h"
#include "sink/RotatingFileSink.h"

class LogWriter {
public:
    LogWriter(BlockingQueue<std::string>& q,
              Stats& stats,
              std::string outPath,
              std::uint64_t rotateMaxBytes,
              int rotateMaxFiles,
              std::size_t batchSize = 256,
              std::chrono::milliseconds flushEvery = std::chrono::milliseconds(50));

    void start();
    void stop();

private:
    void run();

private:
    BlockingQueue<std::string>& q_;
    Stats& stats_;
    RotatingFileSink sink_;
    std::size_t batchSize_;
    std::chrono::milliseconds flushEvery_;
    std::thread th_;
    std::atomic_bool running_{false};
};