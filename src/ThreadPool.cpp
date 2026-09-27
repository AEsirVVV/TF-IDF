// ThreadPool.cpp —— 线程池实现（仅用 C++17 标准库）。
//
// 与 include/ThreadPool.h 中的声明一一对应。
//
// 线程池的核心逻辑：
//   构造：启动 N 个工作线程，每个线程进入 workerLoop 等待任务；
//   提交：submit() 加锁把任务压入队列，notify_one 唤醒一个线程；
//   执行：工作线程在条件变量上等待"有任务 或 要停止"，被唤醒后取出任务，
//         **释放锁再执行**（避免任务体长时间占用锁导致其他线程卡住）；
//   析构：置停止标志 → notify_all 唤醒所有线程 → join 全部线程，优雅退出。

#include "ThreadPool.h"

#include <algorithm>   // std::max

// 构造：创建 threadCount 个工作线程（0 表示按硬件并发度自动决定）。
ThreadPool::ThreadPool(size_t threadCount) {
    if (threadCount == 0) {
        threadCount = static_cast<size_t>(std::thread::hardware_concurrency());
        if (threadCount == 0) {
            threadCount = 2;       // 兜底：拿不到硬件并发度时至少 2 个线程
        }
    }

    workers_.reserve(threadCount);
    for (size_t i = 0; i < threadCount; ++i) {
        // 每个工作线程执行同一个循环（workerLoop）
        workers_.emplace_back([this]() { workerLoop(); });
    }
}

// 析构：优雅停机——不再接新任务，唤醒所有线程并等待它们结束。
ThreadPool::~ThreadPool() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopping_ = true;          // 停止标志：workerLoop 看到后准备退出
    }
    cv_.notify_all();              // 唤醒所有正在等待的工作线程

    for (std::thread& worker : workers_) {
        if (worker.joinable()) {
            worker.join();         // 等待线程真正结束（RAII 式收尾）
        }
    }
}

// 工作线程主循环。
void ThreadPool::workerLoop() {
    for (;;) {
        std::function<void()> task;

        {
            // 等待条件：有任务可做，或者收到停止信号。
            // 条件变量必须配合 unique_lock（wait 期间会临时释放锁）。
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this]() { return stopping_ || !tasks_.empty(); });

            // 停止且队列已空 -> 线程退出（保证已提交的任务不会被丢弃）
            if (stopping_ && tasks_.empty()) {
                return;
            }

            task = std::move(tasks_.front());
            tasks_.pop();
        }   // 作用域结束，锁释放

        // 在锁外执行任务：任务体可能很耗时，持锁执行会让其他线程无法取任务。
        task();
    }
}
