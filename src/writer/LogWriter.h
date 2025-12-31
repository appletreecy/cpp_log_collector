#pragma once
#include <atomic>
#include <cstddef>
#include <string>
#include <thread>

#include "common/BlockingQueue.h"
#include "sink/FileSink.h"

class LogWriter {
public:
    LogWriter(BlockingQueue<std::string>& q, std::string outPath, std::size_t batchSize = 256);

    void start();
    void stop();

private:
    void run();

private:
    BlockingQueue<std::string>& q_;
    FileSink sink_;
    std::size_t batchSize_;
    std::thread th_;
    std::atomic_bool running_{false};
};