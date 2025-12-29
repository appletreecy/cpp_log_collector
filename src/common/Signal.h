#pragma once
#include <atomic>

class Signal {
public:
    // install SIGINT/SIGTERM handlers;
    static void install();

    // check if shutdown requested
    static bool stopRequested();

private:
    static std::atomic<bool> stop_;

    // 👇 allow the C-style signal handler to set the flag
    friend void handleSignal(int);
};