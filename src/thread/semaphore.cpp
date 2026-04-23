#include "semaphore.h"

namespace sylar {
    explicit Semaphore::Semaphore(int count=0) : _count(count) {}

    void Semaphore::wait() {
        // lock the mutex to protect the count variable
        std::unique_lock<std::mutex> lock(_mutex);

        // wait until the count is greater than zero, if the count is zero, it will block the thread and wait for signals
        while (_count == 0) { 
            _cv.wait(lock); // wait for signals
        }
        _count--;
    }

    void Semaphore::signal() {
        // lock the mutex to protect the count variable
        std::unique_lock<std::mutex> lock(_mutex);

        // increase the count and wake up one waiting thread if there are any
        _count++;
        _cv.notify_one();
    }
}