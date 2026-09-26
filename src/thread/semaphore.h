#ifndef _SEMAPHORE_H_
#define _SEMAPHORE_H_

#include <mutex>
#include <functional>     
#include <condition_variable>

namespace sylar {
    /**
     * @brief the Semaphore class is used to control the number of threads that can access a resource or critical section.
     */
    class Semaphore {
    private:
        int _count;                                                 // the number of available resources

        std::mutex _mutex;                                          // mutex to protect the count variable
        std::condition_variable _cv;                                // condition variable to block and wake up threads

    public:
        /**
         * @brief The constructor of the Semaphore class.
         * @param count the initial count value of the semaphore
         * @note 1. Use the `explicit` keyword to prevent implicit type conversions.
         * @note 2. The count value of a semaphore cannot be negative in Linux systems.
         * @note 3. Ensure that the semaphore is shared and visible across multiple threads to enable cross-thread synchronization.
         */
        explicit Semaphore(int count=0);

        /**
         * @brief The method to acquire a semaphore (P-operation).
         * @note 1. During the `Thread` initialization process, if the main thread calls this function, it will remain blocked until the child thread calls `signal()`.
         * @note 2. Due to the context switching and mutex contention associated with condition variables, their overhead in high-frequency, ultra-low-latency scenarios is slightly higher than that of native atomic semaphores; however, the logic is easier to debug and extend.
         */
        void wait();
        /**
         * @brief The method to release a semaphore (V-operation).
         * @note 1. Invoking this function at the end of `Thread::run` serves as the sole key to unblocking the constructor.
         * @note 2. Even if no threads are waiting, the `_count` increment is recorded, ensuring that any subsequent `wait()` calls will proceed without entering a blocked state.
         * @note 3. Invoking `notify_one` after releasing the lock reduces the likelihood of lock contention when the target thread wakes up, due to the internal implementation of `std::condition_variable`.
         */
        void signal();
    };
}

#endif