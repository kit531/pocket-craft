// Small fixed-size worker pool and a mutex-protected queue, shared helpers for world generation/meshing.
#pragma once
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace pc {

class ThreadPool {
public:
    explicit ThreadPool(int threads = 2);
    ~ThreadPool();  // finishes the running task, drops queued ones, joins
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;
    void submit(std::function<void()> task);
    size_t pending() const;

private:
    void workerLoop();
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<std::function<void()>> tasks_;
    std::vector<std::thread> threads_;
    bool stop_ = false;
};

template <typename T>
class ThreadSafeQueue {
public:
    void push(T v) {
        std::lock_guard<std::mutex> l(m_);
        q_.push_back(std::move(v));
    }
    // Appends everything queued to `out`; returns the count.
    size_t drain(std::vector<T>& out) {
        std::lock_guard<std::mutex> l(m_);
        size_t n = q_.size();
        for (auto& v : q_) out.push_back(std::move(v));
        q_.clear();
        return n;
    }
    size_t size() const {
        std::lock_guard<std::mutex> l(m_);
        return q_.size();
    }

private:
    mutable std::mutex m_;
    std::deque<T> q_;
};

}  // namespace pc
