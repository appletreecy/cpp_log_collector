#include "writer/LogWriter.h"

LogWriter::LogWriter(BlockingQueue<std::string>& q,
                     std::string outPath,
                     std::size_t batchSize,
                     std::chrono::milliseconds flushEvery)
    : q_(q), sink_(outPath), batchSize_(batchSize), flushEvery_(flushEvery) {}

void LogWriter::start() {
    running_.store(true, std::memory_order_relaxed);
    th_ = std::thread(&LogWriter::run, this);
}

void LogWriter::stop() {
    q_.close();
    if (th_.joinable()) th_.join();
    running_.store(false, std::memory_order_relaxed);
}

void LogWriter::run() {
    while (true) {
        // Wait up to flushEvery_ for at least one item, then pop up to batchSize_
        auto batch = q_.popBatchFor(batchSize_, flushEvery_);

        if (!batch.empty()) {
            for (auto& line : batch) sink_.writeLine(line);
        }

        // Exit condition: queue is closed AND currently empty
        // We check emptiness via another timed pop would just return empty again.
        if (q_.isClosed() && q_.size() == 0) {
            break;
        }
    }

    sink_.writeLine("Writer stopped.");
}
