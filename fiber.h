#ifndef _COROUTINE_H_   // 防止重复包含
#define _COROUTINE_H_   // 定义宏标识符

#include <mutex>
#include <atomic>
#include <memory>
#include <cassert>
#include <iostream>
#include <unistd.h>
#include <functional>
#include <ucontext.h>

namespace sylar {
    /**
     * @brief a coronine class that offers a coroutine's functionalities, such as creating, destroying, switching, etc.
     * @details The Coroutine class offers functionalities for creation, destruction, and context switching, supporting the reuse and scheduling of coroutines. 
     *  ******  each coroutine possesses a unique ID and a specific state, which can be READY, RUNNING, or TERM
     *  ******  execution can be resumed via the `resume()` method, while control can be yielded via the `yield()` method
     *  ******  offer static methods for retrieving the currently running coroutine and for setting the scheduler coroutine
     * @note The Coroutine class utilizes the ucontext library to implement context switching, supporting coroutine scheduling within a multi-threaded environment. The Coroutine class employs `std::enable_shared_from_this` to facilitate shared pointer functionality, thereby simplifying the management of coroutine object lifetimes.
     */
    class Fiber : public std::enable_shared_from_this<Fiber>{
    public:
        std::mutex _mutex;                                                                          // a mutex to protect the shared data of fiber
        enum State { READY, RUNNING, TERM };                                                        // the states of fiber

        Fiber(std::function<void()> cb, size_t stacksize = 0, bool run_in_scheduler = true);        // the constructor of fiber, used for creating a new fiber,
        ~Fiber();                                                                                   // the destructor of fiber (when the fiber is in TERM state)

        void reset(std::function<void()> cb);                                                       // reuse a fiber and reset the function that the fiber will execute (when the fiber in TERM state)
        void resume();                                                                              // resume the execution of fiber, switch from the running fiber to this fiber (when the fiber in READY state)
        void yield();                                                                               // yield the execution of fiber, switch from this fiber to the scheduler fiber (when the fiber in RUNNING state)

        uint64_t getId() const;                                                                     // get the id of fiber
        State getState() const;                                                                     // get the state of fiber

        static void SetThis(Fiber *f);                                                              // set the current running fiber, private to prevent external call
        static std::shared_ptr<Fiber> GetThis();                                                    // get the current running fiber, private to prevent external call
        static void SetSchedulerFiber(Fiber* f);                                                    // set the scheduler fiber, private to prevent external call

        static uint64_t GetFiberId();                                                               // get the id of current running fiber, called by resume() and yield() methods, private to prevent external call
        static void MainFunc();                                                                     // the main function of fiber, called by the system when the fiber is resumed for the first time, private to prevent external call

    private:
        State m_state = READY;                                                                      // the state of fiber, default is READY

        uint64_t m_id = 0;                                                                          // the id of fiber, assigned by the system
        uint32_t m_stacksize = 0;                                                                   // the size of fiber's stack, default is 128KB

        bool m_runInScheduler;                                                                      // whether to yield execution to the scheduler fiber when yielding, default is true
        ucontext_t m_ctx;                                                                           // the context of fiber, used for switching between fibers

        void* m_stack = nullptr;                                                                    // the stack pointer of fiber, allocated by the system, used for storing the execution context of fiber
        std::function<void()> m_cb;                                                                 // the function that the fiber will execute, defined by the user, can be reset for reuse

        Fiber();                                                                                    // the constructor of fiber, used for creating the main fiber, called only by GetThis() method, private to prevent external instantiation
    };
}

#endif