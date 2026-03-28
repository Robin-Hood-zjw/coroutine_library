#include "semaphore.h"

namespace sylar {
    /**
     * @brief the constructor initializes the count variable with the given value
     */
    explicit Semaphore::Semaphore(int count=0) : _count(count) {}

    /**
     * @brief the wait function will block the calling thread until the count is greater than zero. 
     * @details it uses a unique lock to protect the count variable and a condition variable to wait for signals. When the count is greater than zero, it will decrease the count and continue execution.
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
     * @brief the signal function will increase the count variable and wake up one waiting thread if there are any.
     * @details it uses a unique lock to protect the count variable and a condition variable to notify one waiting thread.
     */
    void Semaphore::signal() {
        // lock the mutex to protect the count variable
        std::unique_lock<std::mutex> lock(_mutex);

        // increase the count and wake up one waiting thread if there are any
        _count++;
        _cv.notify_one();
    }
}