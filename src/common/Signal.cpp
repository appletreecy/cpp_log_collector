#include "common/Signal.h"
#include <csignal>

std::atomic_bool Signal::stop_{false};

void handleSignal(int /*signum*/) {
    Signal::stop_.store(true, std::memory_order_relaxed);
}

void Signal::install() {
    struct sigaction sa;
    sa.sa_handler = handleSignal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0; // do not restart syscalls

    sigaction(SIGINT,  &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);
}

bool Signal::stopRequested() {
    return stop_.load(std::memory_order_relaxed);
}
