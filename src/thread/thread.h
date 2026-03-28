#ifndef _THREAD_H_
#define _THREAD_H_

#include "semaphore.h"

#include <mutex>
#include <functional>     
#include <condition_variable>

namespace sylar {
    class Thread {
    private:
        pid_t _id = -1;                                                     // the ID of the thread
        pthread_t _thread = 0;                                              // the thread handle

        std::string _name;                                                  // the name of the thread

        Semaphore _semaphore;                                               // semaphore for thread synchronization
        std::function<void()> _callback;                                    // the callback function to be executed by the thread

        static void* run(void* arg);                                        // the static function that will be passed to pthread_create
    public:
        Thread(std::function<void()> callback, const std::string& name);    // constructor to initialize the thread
        ~Thread();                                                          // destructor to clean up resources

        pid_t getId() const { return _id; }                                 // getter for the thread ID
        const std::string& getName() const { return _name; }                // getter for the thread name

        void join();                                                        // method to wait for the thread to finish                 

        static pid_t GetThreadId();                                         // method to get the current thread ID
        static Thread* GetThis();                                           // method to get the current thread object

        static const std::string& GetName();                                // method to get the current thread name
        static void SetName(const std::string& name);                       // method to set the current thread name
    };
}

#endif