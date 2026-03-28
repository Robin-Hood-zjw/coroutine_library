#include "thread.h"

#include <iostream>
#include <unistd.h>
#include <sys/syscall.h>

namespace sylar {
    static thread_local Thread* cur_thread = nullptr;                             // thread-local pointer to the current thread object
    static thread_local std::string cur_thread_name = "UNKNOWN";                  // thread-local variable to store the current thread name

    /**
     * @brief The static entry-point function executed by the child thread.
     * @param arg: points to the raw pointer (`this`) of the current `Thread` object
     * @return a `void*` return value of 0 indicates that the thread exited normally
     * @details This function performs several critical tasks:
     *   1. initializes thread-local variables with thread object, name information, ID
     *   2. sets the thread name via platform-specific APIs, ensuring compatibility in Linux and macOS
     *   3. swaps the callback function to minimize reference counting overhead, releasing resources after the callback completes
     *   4. signals the semaphore to indicate that the thread has started and its ID has been set
     *   5. executes the callback function from the user
     */
    void* Thread::run(void* arg) {
        // cast the argument to a Thread pointer
        Thread* thread = (Thread*)arg;

        // set the thread-local variables
        cur_thread = thread;
        cur_thread_name = thread->_name;
        thread->_id = GetThreadId();

        // set the thread name using platform-specific APIs
        #ifdef __APPLE__
            pthread_setname_np(thread->_name.substr(0, 15).c_str());
        #else
            pthread_setname_np(pthread_self(), thread->_name.substr(0, 15).c_str());
        #endif

        // swap the callback function to reduce reference counting overhead
        std::function<void()> callback;
        callback.swap(thread->_callback);

        // signal the semaphore to indicate that the thread has started and the ID has been set
        thread->_semaphore.signal();

        callback();
        return 0;
    }

    /**
     * @brief The constructor of the Thread class is responsible for creating and starting a physical thread.
     * @param callback: The task callback function executed after the thread starts (typically a lambda or `std::bind`)
     * @param name: A descriptive name for the thread; a length of no more than 15 bytes is recommended (due to Linux kernel limitations)
     * @throws std::logic_error if thread creation fails, with an appropriate error message logged to `std::cerr`
     * @details The constructor performs the following steps:
     *   1. use `pthread_create` to create the underlying POSIX thread and use `Thread::run` as the entry point
     *   2. implement the jump from C-style API to C++ class member logic by passing the `this` pointer
     *   3. the constructor calls `_semaphore.wait()` to enter a blocked state
     */
    Thread::Thread(std::function<void()> callback, const std::string& name): 
    _callback(callback), _name(name) {
        // create the thread and pass the static run function as the entry point
        int rt = pthread_create(&_thread, nullptr, &Thread::run, this);
        if (rt) {
            std::cerr << "pthread_create thread fail, rt=" << rt << " name=" << name;
            throw std::logic_error("pthread_create error");
        }

        // wait for the thread to start and set its ID
        _semaphore.wait();
    }

    /**
     * @brief Destructor for the Thread class. If the thread is still running, it detaches the thread to allow resources to be cleaned up when it finishes. It also resets the thread handle to indicate that the thread is no longer active.
     */
    Thread::~Thread() {
        // if the thread is still running, detach it to allow resources to be cleaned up
        if (_thread){
            pthread_detach(_thread);        // detach the thread to allow it to clean up resources when it finishes
            _thread = 0;                    // reset the thread handle to indicate that the thread is no longer active
        }
    }

    /**
     * @brief the join function is used to wait for the thread to finish. If the thread is still running, it calls pthread_join to block the calling thread until the thread finishes. If pthread_join fails, it logs an error message and throws a logic_error exception. After successfully joining the thread, it resets the thread handle to indicate that the thread has finished.
     */
    void Thread::join() {
        // if the thread is still running, join it to wait for it to finish
        if (_thread) {
            // join the thread and wait for it to finish
            int val = pthread_join(_thread, nullptr);
            if (val) {
                std::cerr << "pthread_join failed, rt = " << val << ", name = " << _name << std::endl;
                throw std::logic_error("pthread_join error");
            }

            // reset the thread handle to indicate that the thread has finished
            _thread = 0;
        }
    }
}