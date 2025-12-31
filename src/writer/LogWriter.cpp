#include "writer/LogWriter.h"

LogWriter::LogWriter(BlockingQueue<std::string>& q, std::string outPath, std::size_t batchSize)
    : q_(q), sink_(outPath), batchSize_(batchSize) {}

void LogWriter::start() {
    running_.store(true, std::memory_order_relaxed);
    th_ = std::thread(&LogWriter::run, this);
}

void LogWriter::stop() {
    // Tell writer to finish draining and exit
    q_.close();
    if (th_.joinable()) th_.join();
    running_.store(false, std::memory_order_relaxed);
}

void LogWriter::run() {
    while (true) {
        auto batch = q_.popBatch(batchSize_);
        if (batch.empty()) {
            // closed + empty => exit
            break;
        }
        for (auto& line : batch) {
            sink_.writeLine(line);
        }
    }
    sink_.writeLine("Writer stopped.");
}
