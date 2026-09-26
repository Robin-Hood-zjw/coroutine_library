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
        std::string _name;                                                  // the name of the thread
        pthread_t _thread = 0;                                              // the thread handle

        Semaphore _semaphore;                                               // semaphore for thread synchronization
        std::function<void()> _callback;                                    // the callback function to be executed by the thread

        /**
         * @brief The static entry-point function executed by the child thread.
         * @param arg: points to the raw pointer (`this`) of the current `Thread` object
         * @return `void*`: return value of 0 indicates that the thread exited normally
         */
        static void* run(void* arg);

    public:
        /**
         * @brief The constructor of the Thread class is responsible for creating and starting a physical thread.
         * @param callback: The task callback function executed after the thread starts
         * @param name: A descriptive name for the thread; a length of no more than 15 bytes is recommended (due to Linux kernel limitations)
         * @throws std::logic_error if thread creation fails, with an appropriate error message logged to `std::cerr`
         */
        Thread(std::function<void()> callback, const std::string& name);
        /**
         * @brief The destructor for the Thread class.
         * @note In high-concurrency scenarios, if `Thread` objects are frequently created and destroyed, the `detach` logic here serves to defend against the exhaustion of the process's virtual memory (Virtual Address Space OOM).
         */
        ~Thread();

        /**
         * @brief The getter method to get the Thread's Kernel ID (LWP ID).
         * @return `pid_t`: returns the real process ID assigned by the kernel.
         * @note 1. Due to the use of semaphore synchronization within the constructor, calling this method ensures that `_id` has been successfully written by the child thread, thereby preventing a race condition where the initial value of -1 is read.
         */
        pid_t getId() const;
        /**
         * @brief The getter method to get the name stored in the thread object.
         * @return `const std::string&`: a constant reference to the thread name.
         * @note 1. This name is specified at object creation time and remains synchronized with `cur_thread_name` in `thread_local`.
         */
        const std::string& getName() const;

        /**
         * @brief The method to wait for the thread to complete execution (blocking synchronization).
         * @throws std::logic_error if `pthread_join` fails, with an appropriate error message logged to `std::cerr`
         * @note 1. You cannot join a thread from within its own thread function; doing so will result in a deadlock.
         * @note 2. An error will occur if you attempt to join a thread that has already been joined or detached.
         * @note 3. When developing high-performance frameworks, it is essential to ensure that, prior to object destruction, threads are either explicitly joined or automatically detached by the destructor.
         */
        void join();              

        /**
         * @brief The getter method to retrieve the kernel-level unique identifier (LWP ID) of the current thread.
         * @return pid_t: The kernel thread ID of the current thread. Returns -1 if the call fails.
         * @note 1. The thread ID observed in `top -H` or `htop` matches this return value.
         * @note 2. Recording this ID in a high-performance logging system enables precise tracking of which physical core or thread a task is executing on.
         * @note 3. In the `/proc/self/task/` directory, the folder name corresponds to this ID.
         */
        static pid_t getThreadId();
        /**
         * @brief The method to get a pointer to the currently running Thread object.
         * @return `Thread*`: A pointer to the object for the current thread. If it is the main thread and has not been manually wrapped, it may return `nullptr`.
         * @note 1. This method is conjunct with `thread_local` for optimization, offering fast access speeds.
         * @note 2. The method serves as a crucial bridge connecting "physical threads" and "logical coroutines."
         */
        static Thread* getRunningThread();

        /**
         * @brief The method to get the name of the currently running Thread object.
         * @return `const std::string&`: a constant reference to the current thread's name. If no name set, it returns "UNKNOWN" or the default name for the main thread.
         * @note 1. This method is static and can be called from non-Thread member functions via `sylar::Thread::getName()`.
         * @note 2. Since a reference is returned, access is extremely fast; however, in some cases, care must be taken regarding the lifetime.
         */
        static const std::string& getRunningThreadName();
        /**
         * @brief The method to set the name of the currently running thread.
         * @param name: The new thread name string
         * @note 1. This operation modifies only the application-layer logical name. To simultaneously modify the thread name within the Linux kernel (as displayed in `ps` or `top`), you must additionally call `pthread_setname_np`.
         * @note 2. In scenarios about high-frequency task switching, frequent modification of strings may incur subtle memory overhead; this requires careful trade-offs based on the specific business context.
         */
        static void setRunningThreadName(const std::string& name);
    };
}

#endif