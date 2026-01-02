#include "common/Signal.h"
#include "common/BlockingQueue.h"
#include "common/Stats.h"
#include "server/UdpServer.h"
#include "writer/LogWriter.h"

#include <chrono>
#include <iostream>
#include <thread>

static void metricsLoop(Stats& s, BlockingQueue<std::string>& q) {
    using namespace std::chrono;

    std::uint64_t lastRecv = 0, lastDrop = 0, lastWritten = 0;
    auto lastT = steady_clock::now();

    while (!Signal::stopRequested()) {
        std::this_thread::sleep_for(std::chrono::seconds(5));

        auto now = steady_clock::now();
        double dt = duration<double>(now - lastT).count();
        lastT = now;

        auto recv = s.received.load(std::memory_order_relaxed);
        auto drop = s.dropped.load(std::memory_order_relaxed);
        auto written = s.written.load(std::memory_order_relaxed);
        auto qsize = q.size();

        auto dRecv = recv - lastRecv;
        auto dDrop = drop - lastDrop;
        auto dWritten = written - lastWritten;

        lastRecv = recv;
        lastDrop = drop;
        lastWritten = written;

        std::cerr
            << "[metrics] recv=" << recv
            << " written=" << written
            << " dropped=" << drop
            << " q=" << qsize
            << " | rcv/s=" << (dt > 0 ? dRecv / dt : 0)
            << " wr/s=" << (dt > 0 ? dWritten / dt : 0)
            << " drop/s=" << (dt > 0 ? dDrop / dt : 0)
            << "\n";
    }
}

int main() {
    try {
        Signal::install();

        Stats stats;
        BlockingQueue<std::string> queue(/*capacity=*/10000);

        LogWriter writer(queue, stats, "collector.log",
                         /*batchSize=*/256,
                         std::chrono::milliseconds(50));
        writer.start();

        std::thread metricsThread(metricsLoop, std::ref(stats), std::ref(queue));

        UdpServer server(9000, queue, stats);
        server.run();     // blocks until SIGTERM/SIGINT

        writer.stop();    // drain + exit

        if (metricsThread.joinable()) metricsThread.join();

        std::cerr << "Final: recv=" << stats.received
                  << " written=" << stats.written
                  << " dropped=" << stats.dropped << "\n";
    } catch (const std::exception& e) {
        std::cerr << "Fatal: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
