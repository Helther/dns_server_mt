#pragma once

#include "queue.hpp"
#include <chrono>
#include <functional>
#include <future>
#include <thread>
#include <vector>


/// class for RAII graceful thread stopping
class ThreadJoiner
{
    std::vector<std::thread>& threads;

public:
    ThreadJoiner(std::vector<std::thread>& threads) :
    threads(threads) {}
    ~ThreadJoiner()
    {
        for (auto& thread : threads)
            if (thread.joinable())
                thread.join();
    }
};

/// Thread pool with fixed size of threads, either ideal count of user-provided.
/// it uses global lock-free task queue, and accepts all bindable function call tasks
class ThreadPool
{
    using Task = std::function<void()>;

    void threadWorker()
    {
        while(!done)
        {
            if (auto task = workQueue.dequeue())
                (*task)();
            else if (pollLatency != std::chrono::microseconds::zero())  // if latency was specified in constructor then thread blocks for the time
                std::this_thread::sleep_for(std::chrono::microseconds(pollLatency));
            else
                std::this_thread::yield();
        }
        // finish remaining tasks
        while (auto task = workQueue.dequeue())
            (*task)();
    }

    void initializeThreads()
    {
        try
        {
            for (unsigned i = 0; i < threadNum; ++i)
                threads.emplace_back(&ThreadPool::threadWorker, this);
        } catch (const std::exception& e) {
            done = true;
            throw std::runtime_error(std::string("Error creating thread pool: ") + e.what());
        }
    }

    std::chrono::microseconds pollLatency;
    unsigned threadNum;
    std::atomic_bool done;
    LockFreeQueue<Task> workQueue;
    std::vector<std::thread> threads;
    ThreadJoiner joiner;

public:
    /// optionally provide time in micro-seconds for thread workers to sleep for between task polling
    ThreadPool(std::chrono::microseconds pollLatencyMicroseconds = std::chrono::microseconds::zero()) :
        pollLatency(pollLatencyMicroseconds),
        threadNum(std::max(std::thread::hardware_concurrency(), 1u)),
        done(false), 
        joiner(threads)
    {
        initializeThreads();
    }
    /// provide custom number of threads in the pool
    ThreadPool(unsigned threadNumber, std::chrono::microseconds pollLatencyMicroseconds = std::chrono::microseconds::zero()) :
        pollLatency(pollLatencyMicroseconds),
        threadNum(threadNumber),
        done(false), 
        joiner(threads)
    {
        initializeThreads();
    }
    ~ThreadPool()
    {
        done = true;
    }
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    /// submit awaitable task with future, exception safety is not guaranteed for the task function 
    /// and should be handled inside the provided function internally
    template<typename F, typename ...Args>
    auto awaitSubmit(F&& f, Args&&... args)
    {
        using FuncResType = typename std::result_of<F(Args...)>::type;
        auto task = std::make_shared<std::packaged_task<FuncResType()>>(std::bind(std::forward<F>(f), std::forward<Args>(args)...));
        std::future<FuncResType> res(task.get()->get_future());
        workQueue.enqueue([task]() { (*task)(); });
        return res;
    }

    /// submit non-awaitable task as-is, exception safety is not guaranteed for the task function 
    /// and should be handled inside the provided function internally
    template<typename F, typename ...Args>
    void submit(F&& f, Args&&... args)
    {
        workQueue.enqueue(std::move(std::bind(std::forward<F>(f), std::forward<Args>(args)...)));
        return;
    }
};
