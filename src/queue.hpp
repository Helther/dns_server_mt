#pragma once

#include <deque>
#include <memory>
#include <mutex>
#include <condition_variable>


template<typename T>
class ThreadSafeQueue
{
    std::deque<std::shared_ptr<T>> data;
    mutable std::mutex mutex;
    std::condition_variable cond_var;

public:
    ThreadSafeQueue(){}

    void push(T val)
    {
        std::shared_ptr<T> newData = std::make_shared<T>(val);
        std::lock_guard<std::mutex> lk(mutex);
        data.push_back(newData);
        cond_var.notify_one();
    }

    void wait_pop(T& val)
    {
        std::unique_lock<std::mutex> lk(mutex);
        cond_var.wait(lk, [this]{ return !data.empty(); });
        val = std::move(*data.front());
        data.pop_front();
    }
    // pop w/o waiting for new data
    std::shared_ptr<T> try_pop()
    {
        std::lock_guard<std::mutex> lk(mutex);
        if (data.empty())
            return std::shared_ptr<T>();
        std::shared_ptr<T> val = data.front();
        data.pop_front();
        return val;
    }

    bool empty() const
    {
        std::lock_guard<std::mutex> lk(mutex);
        return data.empty();        
    }
    
};
