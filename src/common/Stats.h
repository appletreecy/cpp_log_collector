#pragma once
#include <atomic>
#include <cstdint>

struct Stats{
    std::atomic<std::uint64_t> received{0};
    std::atomic<std::uint64_t> dropped{0};
    std::atomic<std::uint64_t> written{0};
};