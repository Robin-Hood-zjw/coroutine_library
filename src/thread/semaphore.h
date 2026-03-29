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
        explicit Semaphore(int count=0);                            // constructor to initialize the count

        void wait();                                                // wait for a signal to wake up
        void signal();                                              // signal a waiting thread to wake up
    };
}

#endif