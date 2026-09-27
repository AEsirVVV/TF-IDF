#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <condition_variable>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <thread>
#include <vector>

// ThreadPool：固定大小的线程池（仅用 C++17 标准库）。
//
// 用途：把"互相独立、可并行"的任务提交给一组常驻工作线程执行，
// 避免为每个任务反复创建/销毁线程的开销，并控制并发上限。
//
// 结构（经典线程池三件套）：
//   - 任务队列 tasks_        ：待执行任务（std::function<void()>）
//   - 互斥锁 mutex_          ：保护任务队列与停止标志（多线程共享数据）
//   - 条件变量 cv_           ：队列空时工作线程等待，提交任务时唤醒一个
//
// 关键设计：
//   1. submit() 返回 std::future，调用方可以等结果、也可以等到全部完成；
//   2. 任务在锁外执行（workerLoop 中先取出任务再释放锁），避免任务体
//      串行化、避免长时间持锁；
//   3. 析构时置停止标志 + notify_all 唤醒全部线程 + join，
//      保证"优雅退出"（不会出现线程还在跑、对象已销毁的悬空访问）。
//
// 线程安全说明：任务之间若访问同一份数据，仍需调用方自己保证同步
// （本项目的用法是"每个线程写自己的局部结果，主线程再合并"，天然无竞争）。
class ThreadPool {
public:
    // threadCount = 0 时取 hardware_concurrency()（获取失败则退化为 2）
    explicit ThreadPool(size_t threadCount = 0);
    ~ThreadPool();

    // 线程池不可拷贝/不可移动（内部持有线程与锁）
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    // 提交任务：把可调用对象 f 排入队列，返回 future 以获取返回值/等待完成。
    // 用法：
    //   ThreadPool pool;
    //   auto fut = pool.submit([]{ return 42; });
    //   int v = fut.get();
    // 注意：模板实现放在头文件（C++ 模板需要可见实现）。
    template <class F>
    auto submit(F&& f) -> std::future<decltype(f())> {
        using ReturnType = decltype(f());

        // packaged_task 把"可调用对象"包装成能取出 future 的异步任务
        auto task = std::make_shared<std::packaged_task<ReturnType()>>(std::forward<F>(f));
        std::future<ReturnType> result = task->get_future();

        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (stopping_) {
                throw std::runtime_error("ThreadPool::submit: 线程池已停止");
            }
            // 包一层 void() 便于统一存进队列（返回值通过 future 取）
            tasks_.emplace([task]() { (*task)(); });
        }
        cv_.notify_one();          // 唤醒一个等待中的工作线程
        return result;
    }

    // 工作线程数量
    size_t size() const { return workers_.size(); }

private:
    // 工作线程主循环：取任务 -> 执行任务
    void workerLoop();

    std::vector<std::thread> workers_;          // 常驻工作线程
    std::queue<std::function<void()>> tasks_;   // 任务队列（受 mutex_ 保护）
    std::mutex mutex_;                          // 保护 tasks_ 与 stopping_
    std::condition_variable cv_;                // 队列非空 / 停止时唤醒
    bool stopping_ = false;                     // 停止标志
};

#endif // THREADPOOL_H
