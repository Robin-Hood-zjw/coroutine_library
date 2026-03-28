#ifndef _THREAD_H_
#define _THREAD_H_

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

    /**
     * @brief the Thread class is used to create and manage threads. It provides methods to get thread id, name, and join the thread. It also has static methods to get the current thread and set/get the thread name.
     */
    class Thread {
    private:
        pid_t _id = -1;                                                // the thread id assigned by the system
        std::string _name;                                             // the name of the thread
        pthread_t _thread = 0;                                         // the thread handle returned by pthread_create

        Semaphore _semaphore;                                          // semaphore to synchronize the thread creation and execution
        std::function<void()> _cb;

        static void* run(void* arg);                                   // static function to run the thread, it will call the user-defined callback function
    public:
        Thread(std::function<void()> cb, const std::string& name);     // constructor to create a thread with a callback function and a name
        ~Thread();                                                     // destructor to clean up the thread resources

        pid_t getId() const { return _id; }                            // get the thread id
        const std::string& getName() const { return _name; }           // get the thread name

        void join();                                                   // join the thread, it will block until the thread finishes execution

        static pid_t GetThreadId();                                    // get the thread id assigned by the system, it will return the thread id of the current thread
        static Thread* GetThis();                                      // get the current thread
        static const std::string& GetName();                           // get the name of the current thread
        static void SetName(const std::string& name);                  // set the name of the current thread
    };
}

#endif