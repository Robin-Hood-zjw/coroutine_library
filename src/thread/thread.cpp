#include "thread.h"

#include <iostream>
#include <unistd.h>
#include <sys/syscall.h>

namespace sylar {
    static thread_local Thread* cur_thread = nullptr;                             // thread-local pointer to the currently running thread object
    static thread_local std::string cur_thread_name = "UNKNOWN";                  // thread-local variable to store the name of the currently running thread object

    void* Thread::run(void* arg) {
        // cast the argument to a Thread pointer
        Thread* thread = (Thread*)arg;

        // set the thread-local variables
        cur_thread = thread;
        cur_thread_name = thread->_name;
        thread->_id = getThreadId();

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

    Thread::Thread(std::function<void()> callback, const std::string& name): 
    _callback(callback), _name(name) {
        // create the thread and pass the static run function as the entry point
        int val = pthread_create(&_thread, nullptr, &Thread::run, this);
        if (val) {
            std::cerr << "pthread_create thread fail, returned value = " << val << ", name = " << _name;
            throw std::logic_error("pthread_create error");
        }

        // wait for the thread to start and set its ID
        _semaphore.wait();
    }

    Thread::~Thread() {
        // if the thread is still running, detach it to allow resources to be cleaned up
        if (_thread){
            pthread_detach(_thread);        // detach the thread to allow it to clean up resources when it finishes
            _thread = 0;                    // reset the thread handle to indicate that the thread is no longer active
        }
    }

    pid_t Thread::getId() const {
        return _id;
    }

    const std::string& Thread::getName() const {
        return _name;
    }

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

    pid_t Thread::getThreadId() {
        return syscall(SYS_gettid); // get the thread ID through a system call
    }

    Thread* Thread::getRunningThread() {
        return cur_thread;
    }

    const std::string& Thread::getRunningThreadName() {
        return cur_thread_name;
    }

    void Thread::setRunningThreadName(const std::string& name) {
        if (cur_thread) cur_thread->_name = name;
        cur_thread_name = name;
    }
}