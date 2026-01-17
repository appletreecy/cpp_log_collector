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
#include <fstream>
#include <iostream>
#include <sstream>
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
        << "  --config <file.json>\n"
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

// --------- tiny JSON (flat) parser ---------
// Supports: {"k":123, "x":"str", ...} with optional whitespace.
// No nesting, no escapes, no arrays.
static std::string readAll(const std::string& path) {
    std::ifstream in(path);
    if (!in.is_open()) throw std::runtime_error("Failed to open config: " + path);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

static void skipWs(const std::string& s, std::size_t& i) {
    while (i < s.size() && (s[i] == ' ' || s[i] == '\n' || s[i] == '\r' || s[i] == '\t')) ++i;
}

static bool consume(const std::string& s, std::size_t& i, char c) {
    skipWs(s, i);
    if (i < s.size() && s[i] == c) { ++i; return true; }
    return false;
}

static std::string parseString(const std::string& s, std::size_t& i) {
    skipWs(s, i);
    if (i >= s.size() || s[i] != '"') throw std::runtime_error("Expected '\"' in config");
    ++i;
    std::string out;
    while (i < s.size() && s[i] != '"') {
        // no escape handling (by design)
        out.push_back(s[i++]);
    }
    if (i >= s.size()) throw std::runtime_error("Unterminated string in config");
    ++i;
    return out;
}

static std::string parseNumberToken(const std::string& s, std::size_t& i) {
    skipWs(s, i);
    std::size_t start = i;
    if (i < s.size() && (s[i] == '-' || s[i] == '+')) ++i;
    while (i < s.size() && (s[i] >= '0' && s[i] <= '9')) ++i;
    if (start == i) throw std::runtime_error("Expected number in config");
    return s.substr(start, i - start);
}

static std::unordered_map<std::string, std::string> parseFlatJson(const std::string& text) {
    std::unordered_map<std::string, std::string> kv;
    std::size_t i = 0;

    if (!consume(text, i, '{')) throw std::runtime_error("Config must start with '{'");

    skipWs(text, i);
    if (consume(text, i, '}')) return kv;

    while (true) {
        std::string key = parseString(text, i);
        if (!consume(text, i, ':')) throw std::runtime_error("Expected ':' after key in config");

        skipWs(text, i);
        std::string val;
        if (i < text.size() && text[i] == '"') {
            val = parseString(text, i);
        } else {
            val = parseNumberToken(text, i);
        }

        kv[key] = val;

        skipWs(text, i);
        if (consume(text, i, '}')) break;
        if (!consume(text, i, ',')) throw std::runtime_error("Expected ',' between items in config");
    }

    return kv;
}

static void applyConfigKV(const std::unordered_map<std::string, std::string>& kv, Config& cfg) {
    auto get = [&](const char* k) -> const std::string* {
        auto it = kv.find(k);
        if (it == kv.end()) return nullptr;
        return &it->second;
    };

    if (auto v = get("udp_port")) {
        int x; if (!parseInt(*v, x) || x <= 0) throw std::runtime_error("Bad udp_port");
        cfg.udpPort = x;
    }
    if (auto v = get("metrics_port")) {
        int x; if (!parseInt(*v, x) || x <= 0) throw std::runtime_error("Bad metrics_port");
        cfg.metricsPort = x;
    }
    if (auto v = get("bind_ip")) cfg.bindIp = *v;
    if (auto v = get("out")) cfg.outPath = *v;

    if (auto v = get("queue")) {
        std::size_t x; if (!parseSizeT(*v, x) || x < 1) throw std::runtime_error("Bad queue");
        cfg.queueCap = x;
    }
    if (auto v = get("batch")) {
        std::size_t x; if (!parseSizeT(*v, x) || x < 1) throw std::runtime_error("Bad batch");
        cfg.batchSize = x;
    }
    if (auto v = get("flush_ms")) {
        int x; if (!parseInt(*v, x) || x < 1) throw std::runtime_error("Bad flush_ms");
        cfg.flushMs = x;
    }
    if (auto v = get("rotate_mb")) {
        std::uint64_t x; if (!parseU64(*v, x) || x < 1) throw std::runtime_error("Bad rotate_mb");
        cfg.rotateMb = x;
    }
    if (auto v = get("rotate_files")) {
        int x; if (!parseInt(*v, x) || x < 1) throw std::runtime_error("Bad rotate_files");
        cfg.rotateFiles = x;
    }
}

// CLI overrides (same as Phase 4.1), plus --config to load file first.
static bool parseArgs(int argc, char** argv, Config& cfg) {
    // First pass: find --config
    std::string configPath;
    for (int i = 1; i < argc; ++i) {
        std::string key = argv[i];
        if (key == "--config") {
            if (i + 1 >= argc) { std::cerr << "Missing value for --config\n"; return false; }
            configPath = argv[i + 1];
            break;
        }
        if (key == "--help" || key == "-h") return false;
    }

    // Load config file if provided
    if (!configPath.empty()) {
        auto text = readAll(configPath);
        auto kv = parseFlatJson(text);
        applyConfigKV(kv, cfg);
    }

    // Second pass: apply CLI overrides
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
        } else if (key == "--config") {
            std::string v; if (!needValue(v)) return false;
            // already loaded above; ignore here
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
        server.run();

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
