#pragma once
#include <condition_variable>
#include <cstddef>
#include <chrono>
#include <deque>
#include <mutex>
#include <vector>

template <typename T>
class BlockingQueue {
public:
    explicit BlockingQueue(std::size_t capacity)
        : capacity_(capacity) {}

    // Non-blocking push. Returns false if full or closed.
    bool tryPush(T item) {
        std::unique_lock<std::mutex> lk(mu_);
        if (closed_) return false;
        if (q_.size() >= capacity_) return false;
        q_.push_back(std::move(item));
        lk.unlock();
        cv_.notify_one();
        return true;
    }

    // Blocking batch pop (no timeout): waits until item available or closed+empty.
    std::vector<T> popBatch(std::size_t maxItems) {
        std::unique_lock<std::mutex> lk(mu_);
        cv_.wait(lk, [&] { return closed_ || !q_.empty(); });
        return popLocked_(maxItems);
    }

    // Timed batch pop: waits up to 'timeout' for at least one item.
    // Returns:
    // - some items if available
    // - empty if timeout and no items
    // - empty if closed and empty
    template <class Rep, class Period>
    std::vector<T> popBatchFor(std::size_t maxItems,
                               const std::chrono::duration<Rep, Period>& timeout) {
        std::unique_lock<std::mutex> lk(mu_);
        cv_.wait_for(lk, timeout, [&] { return closed_ || !q_.empty(); });
        // Whether we woke due to timeout or notify, pop what we have (could be empty).
        return popLocked_(maxItems);
    }

    void close() {
        std::lock_guard<std::mutex> lk(mu_);
        closed_ = true;
        cv_.notify_all();
    }

    bool isClosed() const {
        std::lock_guard<std::mutex> lk(mu_);
        return closed_;
    }

    std::size_t size() const {
        std::lock_guard<std::mutex> lk(mu_);
        return q_.size();
    }

private:
    std::vector<T> popLocked_(std::size_t maxItems) {
        std::vector<T> out;
        if (q_.empty()) return out;

        const std::size_t n = (q_.size() < maxItems) ? q_.size() : maxItems;
        out.reserve(n);
        for (std::size_t i = 0; i < n; ++i) {
            out.push_back(std::move(q_.front()));
            q_.pop_front();
        }
        return out;
    }

private:
    const std::size_t capacity_;
    mutable std::mutex mu_;
    std::condition_variable cv_;
    std::deque<T> q_;
    bool closed_{false};
};
