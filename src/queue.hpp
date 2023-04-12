#pragma once

#include <atomic>
#include <deque>
#include <memory>
#include <mutex>
#include <condition_variable>


/// Basic blocking thread-safe queue with mutex and condition_variable with waiting pop functionality
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

    void waitPop(T& val)
    {
        std::unique_lock<std::mutex> lk(mutex);
        cond_var.wait(lk, [this]{ return !data.empty(); });
        val = std::move(*data.front());
        data.pop_front();
    }
    // pop w/o waiting for new data
    std::shared_ptr<T> tryPop()
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

/// Multiple Producer - Multiple Consumer thread-safe lock-free queue with custom type support
/// through atomic pointer template spec, more flexible than boost, that uses atomic<shared_ptr> and requires trivial data types
/// Code has been mostly taken from C++ Concurrency In Action book, with few modifications:
/// bug-fixes in memory managment and ref-counting, wait poping ability
template<typename T>
class LockFreeQueue
{
    struct Node;
    struct CountedPtrNode
    {
        int extCount = 0;  // reference counting reads from all threads
        Node* ptr = nullptr;
    };
    struct NodeCounter
    {
        int intCount = 0;  // ref counting from queue nodes
        int extCounters = 2;  // number of active references (2 max possible (from head or tail ptr and from Node::next))
    };
    struct Node
    {
        std::atomic<T*> data;
        std::atomic<CountedPtrNode> next;
        std::atomic<NodeCounter> count;

        void releaseInternal() noexcept
        {
            NodeCounter oldCounter = count.load(std::memory_order_relaxed);
            NodeCounter newCounter;
            do
            {
                newCounter = oldCounter;
                --newCounter.intCount;
            }
            while (!count.compare_exchange_strong(oldCounter, newCounter, std::memory_order_acquire, std::memory_order_relaxed));
            if (!newCounter.intCount && !newCounter.extCounters)
                delete this;
        }
    };

    // used when thread makes read of node
    static void increaseNodeExtCount(std::atomic<CountedPtrNode>& node, CountedPtrNode& oldNode) noexcept
    {
        CountedPtrNode newNodeCount;
        do 
        {
            newNodeCount = oldNode;
            ++newNodeCount.extCount;
        }
        while (!node.compare_exchange_strong(oldNode, newNodeCount, std::memory_order_acquire, std::memory_order_relaxed));
        oldNode.extCount = newNodeCount.extCount;
    }
    // used when poping or changing tail of queue
    static void freeNodeExtCount(CountedPtrNode& node) noexcept
    {
        Node* const ptr = node.ptr;
        int countIncrease = node.extCount - 2;
        NodeCounter oldCounter = ptr->count.load(std::memory_order_relaxed);
        NodeCounter newCounter;
        do
        {
            newCounter = oldCounter;
            --newCounter.extCounters;
            newCounter.intCount += countIncrease;
        }
        while(!ptr->count.compare_exchange_strong(oldCounter, newCounter, std::memory_order_acquire, std::memory_order_relaxed));
        if (!newCounter.extCounters && !newCounter.intCount)
            delete ptr;
    }
    void setNewTail(CountedPtrNode& oldTail, CountedPtrNode const& newTail) noexcept
    {
        Node* const currentTailPtr = oldTail.ptr;
        while (!tail.compare_exchange_weak(oldTail, newTail) && oldTail.ptr == currentTailPtr);
        if (oldTail.ptr == currentTailPtr)
            freeNodeExtCount(oldTail);
        else
            currentTailPtr->releaseInternal(); 
    }

    void notifyOnEnqueue() noexcept
    {
        isEmpty = false;
        isEmpty.notify_one();
    }

    std::atomic<CountedPtrNode> head;
    std::atomic<CountedPtrNode> tail;
    std::atomic_bool isEmpty;

public:
    LockFreeQueue()
    {
        head.store({0, new Node});
        tail.store(head.load(std::memory_order_relaxed));
    }
    LockFreeQueue(const LockFreeQueue<T>& other) = delete;
    LockFreeQueue<T>& operator=(const LockFreeQueue<T>& other) = delete;
    ~LockFreeQueue()
    {
        while(true)
        {
            CountedPtrNode oldHead = head.load();
            if (oldHead.ptr)
            {
                head.store(oldHead.ptr->next);
                delete oldHead.ptr;
            }
            else
                return;
        }
    }

    void enqueue(const T& newVal)
    {  // can potentially throw
        std::unique_ptr<T> newData = std::make_unique<T>(newVal);
        std::unique_ptr<Node> newNode = std::make_unique<Node>();
        CountedPtrNode newNext{1, newNode.get()};
        CountedPtrNode oldTail = tail.load(std::memory_order_relaxed);
        for(;;)
        {
            increaseNodeExtCount(tail, oldTail);
            T* oldData = nullptr;
            if (oldTail.ptr->data.compare_exchange_strong(oldData, newData.get(), std::memory_order_acquire, std::memory_order_relaxed))
            {  // try update tail data with new one
                CountedPtrNode oldNext;
                if (!oldTail.ptr->next.compare_exchange_strong(oldNext, newNext, std::memory_order_acquire, std::memory_order_relaxed))
                {  // failed to set next ptr, try again
                    oldTail.ptr->releaseInternal();
                    continue;
                }
                else
                {  // successfuly added element
                    newNode.release();  // to prevent unique_ptr<Node> from auto freeing
                    setNewTail(oldTail, newNext);
                    notifyOnEnqueue();
                }
                newData.release();  // to prevent unique_ptr<T> from auto freeing
                return;
            }
            else
            {  // if failed, then help other thread update next ptr
                CountedPtrNode oldNext;
                if (oldTail.ptr->next.compare_exchange_strong(oldNext, newNext, std::memory_order_acquire, std::memory_order_relaxed))
                {  // successfuly added element
                    newNode.release();  // to prevent unique_ptr<Node> from auto freeing
                    newData.release();  // to prevent unique_ptr<T> from auto freeing
                    setNewTail(oldTail, newNext);
                    notifyOnEnqueue();
                    return;
                }
                else  // try to help again
                    oldTail.ptr->releaseInternal();
            }
        }
    }

    /// can return empty ptr
    std::unique_ptr<T> dequeue() noexcept
    {
        CountedPtrNode oldHead = head.load(std::memory_order_relaxed);
        for(;;)
        {
            increaseNodeExtCount(head, oldHead);
            Node* const ptr = oldHead.ptr;
            if (ptr == tail.load().ptr)
            {  // got empty queue, update atomic bool, don't notify waitDequeue, because it's empty
                isEmpty = true;
                //isEmpty.notify_one();
                ptr->releaseInternal();
                return std::unique_ptr<T>();
            }

            CountedPtrNode newNext = ptr->next.load();
            if (head.compare_exchange_strong(oldHead, newNext, std::memory_order_acquire, std::memory_order_relaxed))
            {  // try and loop to update head with  next element until successfull or other thread did it first
                T* const res = ptr->data.exchange(nullptr);
                freeNodeExtCount(oldHead);
                return std::unique_ptr<T>(res);
            }
            ptr->releaseInternal();
        }
    }

    /// can return empty ptr
    std::unique_ptr<T> waitDequeue()
    {
        isEmpty.wait(true);
        return dequeue();
    }
};
