#define _XOPEN_SOURCE 600

#include "fiber.h"
#include <ucontext.h>

static bool debug = false;

namespace sylar {
    static thread_local Fiber* _running_fiber = nullptr;                   // the currently running fiber in the current thread, used for switching between fibers
    static thread_local Fiber* _scheduler_fiber = nullptr;                 // the scheduler fiber in the current thread, used for switching between fibers when yielding (nullptr if not set)
    static thread_local std::shared_ptr<Fiber> _main_fiber = nullptr;      // the main fiber of the current thread, used for switching back to the main fiber when yielding, can be nullptr if not set

    static std::atomic<uint64_t> _fiber_id{0};                             // the id generator for fibers, used for assigning unique ids to fibers
    static std::atomic<uint64_t> _fiber_count{0};                          // the count of fibers, used for tracking the number of fibers in the system

    /**
     * @brief the constructor of fiber
     * @note 1. The constructor initializes the fiber with the execution context.
     * @note 2. If the context setup fails, it outputs an error and terminates the thread.
     * @note 3. The constructor outputs debug information about the creation of the fiber.
     */
    Fiber::Fiber(std::function<void()> cb, size_t stacksize = 0, bool run_in_scheduler = true): 
        _cb(cb), _yieldToSchedulerFiber(run_in_scheduler), _state(READY),
        _stackSize(stacksize ? stacksize : 128000), _stackPtr(malloc(_stackSize)) {
        // check if the allocation works, if not, output an error message and terminate the thread
        if(getcontext(&_ctx) == -1) {
            std::cerr << "Fiber(std::function<void()> cb, size_t stacksize, bool run_in_scheduler) failed\n";
            pthread_exit(NULL);         // terminate the thread if the context setup fails
        }

        // initialize the execution context of fiber
        _ctx.uc_link = nullptr;
        _ctx.uc_stack.ss_sp = _stackPtr;
        _ctx.uc_stack.ss_size = _stackSize;

        // set the main function of fiber to be the MainFunc method (when the fiber is resumed for the first time)
        makecontext(&_ctx, reinterpret_cast<void(*)()>(Fiber::MainFunc), 0);

        // assign a unique id to the fiber and increment the global fiber count
        _id = _fiber_id++;
        _fiber_count++;

        // output debug information about the creation of fiber
        if(debug) std::cout << "Fiber(): child id = " << _id << std::endl;
    }

    /**
     * @brief the destructor of fiber
     * @note 1. The destructor decrements the global fiber count.
     * @note 2. The destructor frees the stack space allocated for the fiber, if it exists.
     * @note 3. The destructor outputs debug information about the destruction of the fiber.
     */
    Fiber::~Fiber() {
        _fiber_count--;

        // free the stack space allocated for the fiber, if it exists
        if(_stackPtr) free(_stackPtr);

        // output debug information about the destruction of fiber
        if(debug) std::cout << "~Fiber(): id = " << _id << std::endl;	
    }

    /**
     * @brief reset a fiber for reuse
     * @note 1. The method checks if the fiber is in the TERM state and has a valid stack pointer before resetting.
     * @note 2. The method resets the state of the fiber to READY and sets the function that the fiber will execute.
     * @note 3. The method initializes the execution context of the fiber.
     * @note 4. The method sets the main function (when the fiber is resumed for the first time).
     */
    void Fiber::reset(std::function<void()> cb) {
        // check if the fiber is in TERM state and has a valid stack pointer
        assert(_stackPtr && _state == TERM);

        // reset the state of fiber to READY and set the function that the fiber will execute, can be reset for reuse
        _state = READY;
        _cb = cb;

        // check if the context setup works, if not, output an error message and terminate the thread
        if(getcontext(&_ctx) == 0) {
            std::cerr << "reset() failed\n";
            pthread_exit(NULL);
        }

        // initialize the execution context of fiber
        _ctx.uc_link = nullptr;
        _ctx.uc_stack.ss_sp = _stackPtr;
        _ctx.uc_stack.ss_size = _stackSize;

        // set the main function of fiber to be the MainFunc method (when the fiber is resumed for the first time)
        makecontext(&_ctx, &Fiber::MainFunc, 0);
    }

    /**
     * @brief resume the execution of fiber
     * @note 1. The method checks if the fiber is in the READY state before resuming. If not, it outputs an error and terminates the thread.
     * @note 2. The method sets the state of the fiber to RUNNING before resuming.
     * @note 3. The method switches from the currently running fiber to this fiber. 
     * @note If a scheduler fiber is set, it switches to the scheduler fiber; otherwise, it switches to the main fiber of the thread.
     */
    void Fiber::resume() {
        // check if the fiber is in READY state
        assert(_state == READY);

        // set the state of fiber to RUNNING
        _state = RUNNING;

        // switch from the currently running fiber to this fiber
        // if a scheduler fiber is set, switch to the scheduler fiber
        // if a scheduler fiber is not set, switch to the main fiber
        if(_yieldToSchedulerFiber) {
            SetThis(this);
            if(swapcontext(&(_scheduler_fiber->_ctx), &_ctx) == -1) {
                std::cerr << "resume() to _scheduler_fiber failed\n";
                pthread_exit(NULL);
            }	
        } else {
            SetThis(this);
            if(swapcontext(&(_main_fiber->_ctx), &_ctx) == -1) {
                std::cerr << "resume() to _main_fiber failed\n";
                pthread_exit(NULL);
            }
        }
    }

    /**
     * @brief yield the execution of fiber
     * @note 1. The method checks if the fiber is in the RUNNING or TERM state before yielding. If not in either, it outputs an error and terminates the thread.
     * @note 2. The method sets the state of the fiber to READY if it is currently in the RUNNING state; otherwise, it keeps the state as TERM. 
     * @note 3. The method switches from this fiber to the scheduler fiber or the main fiber of the thread, depending on if a scheduler fiber is set.
     * @note 4. The method sets the appropriate fiber as the currently running fiber before switching contexts.
     */
    void Fiber::yield() {
        // check if the fiber is in RUNNING or TERM state
        assert(_state == RUNNING || _state == TERM);

        // set the state of fiber to READY if in RUNNING state, otherwise keep it in TERM state
        if (_state != TERM) _state = READY;

        // switch from this fiber to the scheduler fiber if a scheduler fiber is set
        // switch from this fiber to the main fiber if a scheduler fiber is not set
        if(_yieldToSchedulerFiber) {
            SetThis(_scheduler_fiber);
            if(swapcontext(&_ctx, &(_scheduler_fiber->_ctx)) == -1) {
                std::cerr << "yield() to to _scheduler_fiber failed\n";
                pthread_exit(NULL);
            }		
        } else {
            SetThis(_main_fiber.get());
            if(swapcontext(&_ctx, &(_main_fiber->_ctx)) == -1) {
                std::cerr << "yield() to _main_fiber failed\n";
                pthread_exit(NULL);
            }	
        }	
    }

    /**
     * @brief get the id of the fiber
     */
    uint64_t Fiber::getId() const {
        return _id;
    }

    /**
     * @brief get the state of fiber
     */
	Fiber::State Fiber::getState() const {
        return _state;
    }

    /**
     * @brief set the current running fiber
     */
    void Fiber::SetThis(Fiber *f) {
        _running_fiber = f;
    }

    /**
     * @brief set the scheduler fiber
     */
    void Fiber::SetSchedulerFiber(Fiber* f) {
        _scheduler_fiber = f;
    }

    /**
     * @brief get the current running fiber
     * @note 1. If there is a running fiber, it returns a shared pointer to that fiber.
     * @note 2. If no running fiber, it creates a new main fiber for the thread as the running fiber, and returns a shared pointer to it.
     */
    std::shared_ptr<Fiber> Fiber::GetThis() {
        // if there is a running fiber, return a shared pointer to that fiber
        if(_running_fiber) return _running_fiber->shared_from_this();

        // create a new main fiber for the thread as the running fiber
        std::shared_ptr<Fiber> main_fiber(new Fiber());
        _main_fiber = main_fiber;
        _scheduler_fiber = main_fiber.get();

        // and return a shared pointer to the running fiber
        assert(_running_fiber == main_fiber.get());
        return _running_fiber->shared_from_this();
    }

    /**
     * @brief get the id of current running fiber
     * @note If there is a running fiber, it returns the id of that fiber; 
     * @note If there is not a running fiber, it returns -1.
     */
    uint64_t Fiber::GetFiberId() {
        if(_running_fiber) return _running_fiber->getId();
        return (uint64_t)-1;
    }

    /**
     * @brief the main function of fiber, called by the system when the fiber is resumed for the first time
     * @note 1. The method retrieves the running fiber and asserts that it is not null.
     * @note 2. The method executes the function that the fiber is supposed to run, sets the function pointer to null and updates the state of the fiber to TERM.
     * @note 3. After the fiber finishes execution, it yields control back to the scheduler fiber or the main fiber, depending on if a scheduler fiber is set.
     */
    void Fiber::MainFunc() {
        // get the running fiber and assert that it is not null
        std::shared_ptr<Fiber> runningFiber = GetThis();
        assert(runningFiber != nullptr);

        // execute the function that the fiber needs to run, set the function pointer to null and update the state of fiber to TERM
        runningFiber->_cb(); 
        runningFiber->_cb = nullptr;
        runningFiber->_state = TERM;

        // yield control back to the scheduler fiber if a scheduler fiber is set, otherwise yield control back to the main fiber
        auto raw_ptr = runningFiber.get();
        runningFiber.reset(); 
        raw_ptr->yield(); 
    }

    /**
     * @brief the constructor of fiber, used for creating the main fiber, called only by GetThis() method
     * @note 1. The constructor sets the current running fiber to this fiber and initializes the state of the fiber to RUNNING.
     * @note 2. The constructor initializes the execution context of the main fiber and assigns a unique id to it.
     * @note 3. The constructor increments the global fiber count and outputs debug information about the creation of the main fiber.
     * @note 4. The main fiber is created when there is no running fiber, and it serves as the initial context for the thread, allowing other fibers to switch back to it when yielding.
     */
    Fiber::Fiber() {
        // set the running fiber to this fiber and initialize the state of fiber to RUNNING
        SetThis(this);
        _state = RUNNING;

        // initialize the execution context of main fiber
        if(getcontext(&_ctx) == -1) {
            std::cerr << "Fiber() failed\n";
            pthread_exit(NULL);
        }

        // assign a unique id to the main fiber and increment the global fiber count
        _id = _fiber_id++;
        _fiber_count ++;

        // output debug information about the creation of main fiber
        if(debug) std::cout << "Fiber(): main id = " << _id << std::endl;
    }
};