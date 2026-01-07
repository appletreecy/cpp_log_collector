#include "writer/LogWriter.h"

LogWriter::LogWriter(BlockingQueue<std::string>& q,
                     Stats& stats,
                     std::string outPath,
                     std::uint64_t rotateMaxBytes,
                     int rotateMaxFiles,
                     std::size_t batchSize,
                     std::chrono::milliseconds flushEvery)
    : q_(q),
      stats_(stats),
      sink_(std::move(outPath), rotateMaxBytes, rotateMaxFiles),
      batchSize_(batchSize),
      flushEvery_(flushEvery) {}

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
        auto batch = q_.popBatchFor(batchSize_, flushEvery_);

        if (!batch.empty()) {
            for (auto& line : batch) sink_.writeLine(line);
            stats_.written.fetch_add(batch.size(), std::memory_order_relaxed);
        }

        if (q_.isClosed() && q_.size() == 0) break;
    }
}
