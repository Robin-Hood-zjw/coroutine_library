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
     * @param callback: The task callback function executed after the thread starts
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
        int val = pthread_create(&_thread, nullptr, &Thread::run, this);
        if (val) {
            std::cerr << "pthread_create thread fail, returned value = " << val << ", name = " << _name;
            throw std::logic_error("pthread_create error");
        }

        // wait for the thread to start and set its ID
        _semaphore.wait();
    }

    /**
     * @brief Destructor for the Thread class.
     * @details The destructor performs the following steps:
     *   1. clean up low-level resources associated with threads
     *   2. invoke `pthread_detach` to reclaim the thread's memory if a Thread object is destructed while it is still active
     *   3. set the thread handle to 0 upon the operation completion
     * @note In high-concurrency scenarios, if `Thread` objects are frequently created and destroyed, the `detach` logic here serves to defend against the exhaustion of the process's virtual memory (Virtual Address Space OOM).
     */
    Thread::~Thread() {
        // if the thread is still running, detach it to allow resources to be cleaned up
        if (_thread){
            pthread_detach(_thread);        // detach the thread to allow it to clean up resources when it finishes
            _thread = 0;                    // reset the thread handle to indicate that the thread is no longer active
        }
    }

    /**
     * @brief The method to wait for the thread to complete execution (blocking synchronization).
     * @throws std::logic_error if `pthread_join` fails, with an appropriate error message logged to `std::cerr`
     * @details This function performs the following steps:
     *   1. invoke `pthread_join` to block the calling thread if the thread is still running
     *   2. pop an error if `pthread_join` fails, with an appropriate error message logged to `std::cerr`
     *   3. reset the thread handle to 0 upon the operation completion
     * @note 
     *   1. You cannot join a thread from within its own thread function; doing so will result in a deadlock.
     *   2. An error will occur if you attempt to join a thread that has already been joined or detached.
     *   3. When developing high-performance frameworks, it is essential to ensure that, prior to object destruction, threads are either explicitly joined or automatically detached by the destructor.
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

    /**
     * @brief The method to retrieve the kernel-level unique identifier (LWP ID) of the current thread.
     * @return pid_t: The kernel thread ID of the current thread. Returns -1 if the call fails.
     * @details This function performs several critical tasks:
     *   1. request the ID of the current thread from the Linux kernel by executing `syscall(SYS_gettid)`
     *   2. returns the actual Process ID (PID) recognized by the kernel scheduler
     * @note `pthread_self()` returns a handle (memory address) maintained by the POSIX threads library, but this function returns the actual Process ID (PID) recognized by the kernel scheduler. In Linux, the kernel thread ID (LWP ID) is used for scheduling and is what you see in system monitoring tools like `top` or `htop`. This distinction is important for debugging and performance analysis, as the kernel thread ID is what the operating system uses to manage threads.\n
     *  -scenarios:
     *    - The thread ID observed in `top -H` or `htop` matches this return value.
     *    - Recording this ID in a high-performance logging system enables precise tracking of which physical core or thread a task is executing on.
     *   - In the `/proc/self/task/` directory, the folder name corresponds to this ID.
     */
    pid_t Thread::GetThreadId() {
        return syscall(SYS_gettid); // get the thread ID using a system call to retrieve the kernel thread ID (LWP ID)
    }

    /**
     * @brief The method to get a pointer to the currently running Thread object.
     * @return `Thread*`: A pointer to the object for the current thread. If it is the main thread and has not been manually wrapped, it may return `nullptr`.
     * @note
     *   1. This method is conjunct with `thread_local` for optimization, offering fast access speeds.
     *   2. The method serves as a crucial bridge connecting "physical threads" and "logical coroutines."
     */
    Thread* Thread::GetThis() {
        return cur_thread;
    }

    /**
     * @brief The method to get the name of the current thread.
     * @return `const std::string&`: a constant reference to the current thread's name. If no name set, it returns "UNKNOWN" or the default name for the main thread.
     * @note
     *    1. This method is static and can be called from non-Thread member functions via `sylar::Thread::GetName()`.
     *    2. Since a reference is returned, access is extremely fast; however, in some cases, care must be taken regarding the lifetime.
     */
    const std::string& Thread::GetName() {
        return cur_thread_name;
    }

    /**
     * @brief The method to set the name of the currently running thread.
     * @param name: The new thread name string
     * @details This function performs the following steps:
     *   1. update the current thread's name if the current thread object exists
     *   2. update the thread-local variable `cur_thread_name` to ensure that subsequent calls to `GetName()` return the updated name
     * @note
     *    1. This operation modifies only the application-layer logical name. To simultaneously modify the thread name within the Linux kernel (as displayed in `ps` or `top`), you must additionally call `pthread_setname_np`.
     *    2. In scenarios about high-frequency task switching, frequent modification of strings may incur subtle memory overhead; this requires careful trade-offs based on the specific business context.
     */
    void Thread::SetName(const std::string& name) {
        if (cur_thread) cur_thread->_name = name;
        cur_thread_name = name;
    }
}