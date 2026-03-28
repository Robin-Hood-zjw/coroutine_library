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
        int count;                                                  // the number of available resources

        std::mutex _mutex;                                          // mutex to protect the count variable
        std::condition_variable cv;                                 // condition variable to block and wake up threads
    public:
        explicit Semaphore(int count_ = 0) : count(count_) {}       // constructor to initialize the count

        void wait();                                                // wait for a signal, it will block if the count is zero, otherwise it will decrease the count and continue execution
        void signal();                                              // signal a waiting thread, it will increase the count and wake up one waiting thread if there are any
    };
}

#endif