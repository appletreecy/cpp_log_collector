#include "common/Signal.h"

#include <csignal>
#include <unistd.h>
#include <fcntl.h>
#include <cstdint>
#include <stdexcept>

std::atomic_bool Signal::stop_{false};
int Signal::pipefd_[2] = {-1, -1};

static void setNonBlocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags >= 0) (void)fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static void setCloExec(int fd) {
    int flags = fcntl(fd, F_GETFD, 0);
    if (flags >= 0) (void)fcntl(fd, F_SETFD, flags | FD_CLOEXEC);
}

// async-signal-safe handler: call public method + write 1 byte to wake poll()
static void handleSignal(int /*signum*/) {
    Signal::requestStopFromSignal();
}

void Signal::requestStopFromSignal() {
    stop_.store(true, std::memory_order_relaxed);

    int wfd = wakeWriteFd();
    if (wfd != -1) {
        const uint8_t b = 1;
        (void)write(wfd, &b, 1); // ignore EAGAIN etc.
    }
}

void Signal::install() {
    if (pipe(pipefd_) != 0) {
        throw std::runtime_error("pipe() failed");
    }

    setNonBlocking(pipefd_[0]);
    setNonBlocking(pipefd_[1]);
    setCloExec(pipefd_[0]);
    setCloExec(pipefd_[1]);

    struct sigaction sa;
    sa.sa_handler = handleSignal;

    // IMPORTANT on macOS: sigemptyset is a macro, don't write ::sigemptyset
    sigemptyset(&sa.sa_mask);

    sa.sa_flags = 0;

    (void)sigaction(SIGINT,  &sa, nullptr);
    (void)sigaction(SIGTERM, &sa, nullptr);
}

bool Signal::stopRequested() {
    return stop_.load(std::memory_order_relaxed);
}

int Signal::wakeFd() {
    return pipefd_[0];
}

int Signal::wakeWriteFd() {
    return pipefd_[1];
}
