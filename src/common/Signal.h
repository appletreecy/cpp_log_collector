#pragma once
#include <atomic>

class Signal {
public:
    static void install();
    static bool stopRequested();

    // poll/select watches this (read end)
    static int wakeFd();

    // used by handler to write 1 byte (write end)
    static int wakeWriteFd();

    // called from signal handler (public so handler doesn't touch private members)
    static void requestStopFromSignal();

private:
    static std::atomic_bool stop_;
    static int pipefd_[2]; // [0] read, [1] write
};
