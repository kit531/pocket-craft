#include "thread_pool.h"

namespace pc {

ThreadPool::ThreadPool(int threads) {
    for (int i = 0; i < threads; ++i) threads_.emplace_back([this] { workerLoop(); });
}

ThreadPool::~ThreadPool() {
    {
        std::lock_guard<std::mutex> l(mutex_);
        stop_ = true;
        tasks_.clear();
    }
    cv_.notify_all();
    for (auto& t : threads_) t.join();
}

void ThreadPool::submit(std::function<void()> task) {
    {
        std::lock_guard<std::mutex> l(mutex_);
        tasks_.push_back(std::move(task));
    }
    cv_.notify_one();
}

size_t ThreadPool::pending() const {
    std::lock_guard<std::mutex> l(mutex_);
    return tasks_.size();
}

void ThreadPool::workerLoop() {
    for (;;) {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> l(mutex_);
            cv_.wait(l, [this] { return stop_ || !tasks_.empty(); });
            if (stop_) return;
            task = std::move(tasks_.front());
            tasks_.pop_front();
        }
        task();
    }
}

}  // namespace pc
