#include "semaphore.h"

namespace sylar {
    /**
     * @brief the wait function will block the calling thread until the count is greater than zero. 
     * @details it uses a unique lock to protect the count variable and a condition variable to wait for signals. When the count is greater than zero, it will decrease the count and continue execution.
     */
    void Semaphore::wait() {
        // lock the mutex to protect the count variable
        std::unique_lock<std::mutex> lock(_mutex);

        // 
        while (count == 0) { 
            cv.wait(lock); // wait for signals
        }
        count--;
    }

    void Semaphore::signal() {
        std::unique_lock<std::mutex> lock(_mutex);

        count++;
        cv.notify_one();
    }
}