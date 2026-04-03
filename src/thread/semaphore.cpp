#include "semaphore.h"

namespace sylar {
    /**
     * @brief The constructor of the Semaphore class.
     * @param count the initial count value of the semaphore. It defaults to 0, meaning that any threads attempting to `wait` in the initial state will be blocked.
     * @note 1. Use the `explicit` keyword to prevent implicit type conversions.
     * @note 2. The count value of a semaphore cannot be negative in Linux systems.
     * @note 3. Ensure that the semaphore is shared and visible across multiple threads to enable cross-thread synchronization.
     */
    explicit Semaphore::Semaphore(int count=0) : _count(count) {}

    /**
     * @brief The method to acquire a semaphore (P-operation / Consume Resource).
     * @note 1. During the `Thread` initialization process, if the main thread calls this function, it will remain blocked until the child thread calls `signal()`.
     * @note 2. Due to the context switching and mutex contention associated with condition variables, their overhead in high-frequency, ultra-low-latency scenarios is slightly higher than that of native atomic semaphores; however, the logic is easier to debug and extend.
     */
    void Semaphore::wait() {
        // lock the mutex to protect the count variable
        std::unique_lock<std::mutex> lock(_mutex);

        // wait until the count is greater than zero, if the count is zero, it will block the thread and wait for signals
        while (_count == 0) { 
            _cv.wait(lock); // wait for signals
        }
        _count--;
    }

    /**
     * @brief The method to release a semaphore (V-operation / Increment Resource Count).
     * @note 1. Invoking this function at the end of `Thread::run` serves as the sole key to unblocking the constructor.
     * @note 2. Even if no threads are waiting, the `_count` increment is recorded, ensuring that any subsequent `wait()` calls will proceed without entering a blocked state.
     * @note 3. Invoking `notify_one` after releasing the lock reduces the likelihood of lock contention when the target thread wakes up, due to the internal implementation of `std::condition_variable`.
     */
    void Semaphore::signal() {
        // lock the mutex to protect the count variable
        std::unique_lock<std::mutex> lock(_mutex);

        // increase the count and wake up one waiting thread if there are any
        _count++;
        _cv.notify_one();
    }
}