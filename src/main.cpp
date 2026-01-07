// src/main.cpp
#include "common/Signal.h"
#include "common/BlockingQueue.h"
#include "common/Stats.h"

#include "writer/LogWriter.h"
#include "server/UdpServer.h"
#include "metrics/MetricsServer.h"

#include <chrono>
#include <iostream>

int main() {
    try {
        // Install SIGINT/SIGTERM handlers + self-pipe wakeup
        Signal::install();

        Stats stats;

        // Tune these later
        BlockingQueue<std::string> queue(/*capacity=*/10000);

        // Rotation 5MB max per file, keep 5 files
        std::uint64_t rotateMaxBytes = 1ULL*1024*1024;
        int rotateMaxFiles = 3;


        LogWriter writer(queue, stats, "collector.log", rotateMaxBytes, rotateMaxFiles,
                         /*batchSize=*/256,
                         std::chrono::milliseconds(50));
        writer.start();

        // HTTP metrics on localhost:9100
        MetricsServer metrics(stats, queue, "127.0.0.1", 9100);
        metrics.start();

        // UDP ingestion on port 9000
        UdpServer server(9000, queue, stats);

        // Blocks until SIGTERM/SIGINT
        server.run();

        // Graceful shutdown order:
        // 1) stop UDP loop returned already
        // 2) stop writer (drain queue)
        // 3) stop metrics server
        writer.stop();
        metrics.stop();

        std::cerr << "Final stats: "
                  << "recv=" << stats.received.load(std::memory_order_relaxed)
                  << " written=" << stats.written.load(std::memory_order_relaxed)
                  << " dropped=" << stats.dropped.load(std::memory_order_relaxed)
                  << "\n";

    } catch (const std::exception& e) {
        std::cerr << "Fatal: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
