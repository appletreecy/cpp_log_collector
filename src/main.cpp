// src/main.cpp
#include "common/Signal.h"
#include "common/BlockingQueue.h"
#include "common/Stats.h"
#include "server/UdpServer.h"
#include "writer/LogWriter.h"
#include "metrics/MetricsServer.h"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>
#include <unordered_map>

struct Config {
    int udpPort = 9000;
    int metricsPort = 9100;
    std::string bindIp = "127.0.0.1";
    std::string outPath = "collector.log";

    std::size_t queueCap = 10000;
    std::size_t batchSize = 256;
    int flushMs = 50;

    std::uint64_t rotateMb = 5; // per file
    int rotateFiles = 5;
};

static void printUsage(const char* prog) {
    std::cerr
        << "Usage: " << prog << " [options]\n"
        << "Options:\n"
        << "  --udp-port <int>         (default 9000)\n"
        << "  --metrics-port <int>     (default 9100)\n"
        << "  --bind-ip <ip>           (default 127.0.0.1)\n"
        << "  --out <path>             (default collector.log)\n"
        << "  --queue <int>            (default 10000)\n"
        << "  --batch <int>            (default 256)\n"
        << "  --flush-ms <int>         (default 50)\n"
        << "  --rotate-mb <int>        (default 5)\n"
        << "  --rotate-files <int>     (default 5)\n"
        << "  --help\n";
}

static bool parseInt(const std::string& s, int& out) {
    char* end = nullptr;
    long v = std::strtol(s.c_str(), &end, 10);
    if (!end || *end != '\0') return false;
    out = static_cast<int>(v);
    return true;
}

static bool parseSizeT(const std::string& s, std::size_t& out) {
    char* end = nullptr;
    unsigned long long v = std::strtoull(s.c_str(), &end, 10);
    if (!end || *end != '\0') return false;
    out = static_cast<std::size_t>(v);
    return true;
}

static bool parseU64(const std::string& s, std::uint64_t& out) {
    char* end = nullptr;
    unsigned long long v = std::strtoull(s.c_str(), &end, 10);
    if (!end || *end != '\0') return false;
    out = static_cast<std::uint64_t>(v);
    return true;
}

static bool parseArgs(int argc, char** argv, Config& cfg) {
    for (int i = 1; i < argc; ++i) {
        std::string key = argv[i];

        auto needValue = [&](std::string& val) -> bool {
            if (i + 1 >= argc) {
                std::cerr << "Missing value for " << key << "\n";
                return false;
            }
            val = argv[++i];
            return true;
        };

        if (key == "--help" || key == "-h") {
            return false;
        } else if (key == "--udp-port") {
            std::string v; if (!needValue(v)) return false;
            if (!parseInt(v, cfg.udpPort) || cfg.udpPort <= 0) { std::cerr << "Bad --udp-port\n"; return false; }
        } else if (key == "--metrics-port") {
            std::string v; if (!needValue(v)) return false;
            if (!parseInt(v, cfg.metricsPort) || cfg.metricsPort <= 0) { std::cerr << "Bad --metrics-port\n"; return false; }
        } else if (key == "--bind-ip") {
            std::string v; if (!needValue(v)) return false;
            cfg.bindIp = v;
        } else if (key == "--out") {
            std::string v; if (!needValue(v)) return false;
            cfg.outPath = v;
        } else if (key == "--queue") {
            std::string v; if (!needValue(v)) return false;
            if (!parseSizeT(v, cfg.queueCap) || cfg.queueCap < 1) { std::cerr << "Bad --queue\n"; return false; }
        } else if (key == "--batch") {
            std::string v; if (!needValue(v)) return false;
            if (!parseSizeT(v, cfg.batchSize) || cfg.batchSize < 1) { std::cerr << "Bad --batch\n"; return false; }
        } else if (key == "--flush-ms") {
            std::string v; if (!needValue(v)) return false;
            if (!parseInt(v, cfg.flushMs) || cfg.flushMs < 1) { std::cerr << "Bad --flush-ms\n"; return false; }
        } else if (key == "--rotate-mb") {
            std::string v; if (!needValue(v)) return false;
            if (!parseU64(v, cfg.rotateMb) || cfg.rotateMb < 1) { std::cerr << "Bad --rotate-mb\n"; return false; }
        } else if (key == "--rotate-files") {
            std::string v; if (!needValue(v)) return false;
            if (!parseInt(v, cfg.rotateFiles) || cfg.rotateFiles < 1) { std::cerr << "Bad --rotate-files\n"; return false; }
        } else {
            std::cerr << "Unknown option: " << key << "\n";
            return false;
        }
    }
    return true;
}

int main(int argc, char** argv) {
    Config cfg;
    if (!parseArgs(argc, argv, cfg)) {
        printUsage(argv[0]);
        return 2;
    }

    try {
        Signal::install();

        Stats stats;
        BlockingQueue<std::string> queue(cfg.queueCap);

        const std::uint64_t rotateMaxBytes = cfg.rotateMb * 1024ULL * 1024ULL;

        LogWriter writer(queue, stats, cfg.outPath,
                         rotateMaxBytes, cfg.rotateFiles,
                         cfg.batchSize,
                         std::chrono::milliseconds(cfg.flushMs));
        writer.start();

        MetricsServer metrics(stats, queue, cfg.bindIp, cfg.metricsPort);
        metrics.start();

        UdpServer server(cfg.udpPort, queue, stats);
        server.run(); // blocks until SIGTERM/SIGINT

        writer.stop();
        metrics.stop();

        std::cerr << "Final stats: recv=" << stats.received.load(std::memory_order_relaxed)
                  << " written=" << stats.written.load(std::memory_order_relaxed)
                  << " dropped=" << stats.dropped.load(std::memory_order_relaxed)
                  << "\n";

    } catch (const std::exception& e) {
        std::cerr << "Fatal: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
