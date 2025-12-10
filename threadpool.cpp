#include "threadpool.h"
using namespace std;

ThreadPool::ThreadPool(size_t n_threads) : stop_(false) {
    if (n_threads == 0) n_threads = 1;
    for (size_t i = 0; i < n_threads; ++i) {
        workers_.emplace_back(&ThreadPool::worker_loop, this);
    }
}

ThreadPool::~ThreadPool() {
    {
      lock_guard<mutex> lk(mutex_);
        stop_ = true;
    }
    cv_.notify_all();
    for (auto &t : workers_) {
        if (t.joinable()) t.join();
    }
}

void ThreadPool::enqueue(function<void()> task) {
    {
        lock_guard<mutex> lk(mutex_);
        tasks_.push(move(task));
    }
    cv_.notify_one();
}

void ThreadPool::worker_loop() {
    for (;;) {
        function<void()> task;
        {
            unique_lock<mutex> lk(mutex_);
            cv_.wait(lk, [this] { return stop_ || !tasks_.empty(); });
            if (stop_ && tasks_.empty()) return;
            task = move(tasks_.front());
            tasks_.pop();
        }
        task();
    }
}
