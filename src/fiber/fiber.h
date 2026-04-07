#ifndef _COROUTINE_H_   // prevent multiple inclusions of the header file
#define _COROUTINE_H_   // define the macro to indicate that the header file has been included

#include <mutex>
#include <atomic>
#include <memory>
#include <cassert>
#include <iostream>
#include <unistd.h>
#include <ucontext.h>
#include <functional>

namespace sylar {
    /**
     * @brief a coronine class that offers a coroutine's functionalities, such as creating, destroying, switching, etc.
     */
    class Fiber : public std::enable_shared_from_this<Fiber>{
    public:
        std::mutex _mutex;                                                                          // a mutex to protect the shared data of fiber
        enum State { READY, RUNNING, TERM };                                                        // the states of fiber (READY: the fiber is ready to run but not running yet, RUNNING: the fiber is currently running, TERM: the fiber has finished execution)

        /**
         * @brief the constructor of fiber
         * @param[in] cb the entry function of a coroutine
         * @param[in] stacksize the stack size of fiber
         * @param[in] run_in_scheduler a sign that indicates if a scheduler fiber is needed
         */
        Fiber(std::function<void()> cb, size_t stacksize = 0, bool run_in_scheduler = true);
        /**
         * @brief the destructor of fiber
         */
        ~Fiber();                                                                                   

        /**
         * @brief reset the state and the entry function of a fiber
         * @param[in] cb the new entry function of the fiber
         * @note 1. reuse the stack space
         * @note 2. do not recreate a stack
         */
        void reset(std::function<void()> cb);
        /**
         * @brief resume the execution of the current fiber which is in READY state
         */
        void resume();
        /**
         * @brief yield the execution of the current fiber which is in RUNNING or TERM state
         */
        void yield();                                                                               

        /**
         * @brief get the id of the fiber
         * @return `uint64_t`: the id of the fiber
         */
        uint64_t getId() const;
        /**
         * @brief get the state of fiber
         * @return `State`: the state of fiber
         */
        State getState() const;

        /**
         * @brief set the current running fiber
         * @param[in] f the fiber to be set as the currently running fiber
         */
        static void setRunningFiber(Fiber *f);
        /**
         * @brief set the scheduler fiber
         * @param[in] f the fiber to be set as the scheduler fiber
         */
        static void setSchedulerFiber(Fiber* f);
        /**
         * @brief get the currently running fiber
         * @return `std::shared_ptr<Fiber>`: the currently running fiber
         */
        static std::shared_ptr<Fiber> getRunningFiber();

        /**
         * @brief get the id of current running fiber
         * @return `uint64_t`: the id of current running fiber
         */
        static uint64_t getFiberId();
        /**
         * @brief the main function of fiber, called by the system when the fiber is resumed for the first time
         */
        static void MainFunc();

    private:
        ucontext_t _ctx;                                                                            // the context of the fiber
        State _state = READY;                                                                       // the state of fiber, default is READY

        uint64_t _id = 0;                                                                           // the id of the fiber
        uint32_t _stackSize = 0;                                                                    // the stack size of the fiber

        bool _yieldToSchedulerFiber;                                                                // a sign that indicates whether to yield execution to the scheduler fiber when yielding

        void* _stackPtr = nullptr;                                                                  // the stack pointer of fiber, allocated by the system, used for storing the execution context of fiber
        std::function<void()> _cb;                                                                  // the function that the fiber will execute, can be reset for reuse

        /**
         * @brief the constructor of fiber to create a main fiber
         * @note 1. only for creating the first fiber in the thread
         * @note 2. only called by getRunningFiber()
         */
        Fiber();
    };
}

#endif