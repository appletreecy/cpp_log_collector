#pragma once
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <optional>
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

    // Pop up to maxItems. Blocks until:
    // - there is at least one item, OR
    // - closed and empty (returns empty vector)
    std::vector<T> popBatch(std::size_t maxItems) {
        std::unique_lock<std::mutex> lk(mu_);
        cv_.wait(lk, [&] { return closed_ || !q_.empty(); });

        std::vector<T> out;
        if (q_.empty()) return out; // closed + empty

        const std::size_t n = (q_.size() < maxItems) ? q_.size() : maxItems;
        out.reserve(n);
        for (std::size_t i = 0; i < n; ++i) {
            out.push_back(std::move(q_.front()));
            q_.pop_front();
        }
        return out;
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
    const std::size_t capacity_;
    mutable std::mutex mu_;
    std::condition_variable cv_;
    std::deque<T> q_;
    bool closed_{false};
};
