#define _XOPEN_SOURCE 600

#include "fiber.h"
#include <ucontext.h>

static bool debug = false;

namespace sylar {
    static thread_local Fiber* _running_fiber = nullptr;                   // the running fiber in the current thread
    static thread_local Fiber* _scheduler_fiber = nullptr;                 // the scheduler fiber in the current thread
    static thread_local std::shared_ptr<Fiber> _main_fiber = nullptr;      // the main fiber of the current thread

    static std::atomic<uint64_t> _fiber_id{0};                             // the id generator for fibers, used for assigning unique ids to fibers
    static std::atomic<uint64_t> _fiber_count{0};                          // the count of fibers, used for tracking the number of fibers in the system

    Fiber::Fiber(std::function<void()> cb, size_t stacksize = 0, bool run_in_scheduler = true): 
        m_cb(cb), m_yieldToSchedulerFiber(run_in_scheduler), m_state(READY),
        m_stackSize(stacksize ? stacksize : 128000), m_stackPtr(malloc(m_stackSize)) {
        // check if the allocation works, if not, output an error message and terminate the thread
        if(getcontext(&m_ctx) == -1) {
            std::cerr << "Fiber(std::function<void()> cb, size_t stacksize, bool run_in_scheduler) failed\n";
            pthread_exit(NULL);         // terminate the thread if the context setup fails
        }

        // initialize the execution context of fiber
        m_ctx.uc_link = nullptr;
        m_ctx.uc_stack.ss_sp = m_stackPtr;
        m_ctx.uc_stack.ss_size = m_stackSize;

        // set the main function of fiber to be the MainFunc method (when the fiber is resumed for the first time)
        makecontext(&m_ctx, reinterpret_cast<void(*)()>(Fiber::MainFunc), 0);

        // assign a unique id to the fiber and increment the global fiber count
        m_id = _fiber_id++;
        _fiber_count++;

        // output debug information about the creation of fiber
        if(debug) std::cout << "Fiber(): child id = " << m_id << std::endl;
    }

    Fiber::~Fiber() {
        _fiber_count--;

        // free the stack space allocated for the fiber, if it exists
        if(m_stackPtr) free(m_stackPtr);

        // output debug information about the destruction of fiber
        if(debug) std::cout << "~Fiber(): id = " << m_id << std::endl;	
    }

    void Fiber::reset(std::function<void()> cb) {
        // check if the fiber is in TERM state and has a valid stack pointer
        assert(m_stackPtr && m_state == TERM);

        // reset the state of fiber to READY and set the function that the fiber will execute, can be reset for reuse
        m_state = READY;
        m_cb = cb;

        // check if the context setup works, if not, output an error message and terminate the thread
        if(getcontext(&m_ctx) == 0) {
            std::cerr << "reset() failed\n";
            pthread_exit(NULL);
        }

        // initialize the execution context of fiber
        m_ctx.uc_link = nullptr;
        m_ctx.uc_stack.ss_sp = m_stackPtr;
        m_ctx.uc_stack.ss_size = m_stackSize;

        // set the main function of fiber to be the MainFunc method (when the fiber is resumed for the first time)
        makecontext(&m_ctx, &Fiber::MainFunc, 0);
    }

    void Fiber::resume() {
        // check if the fiber is in READY state
        assert(m_state == READY);

        // set the state of fiber to RUNNING
        m_state = RUNNING;

        // switch from the currently running fiber to this fiber
        // if a scheduler fiber is set, switch to the scheduler fiber
        // if a scheduler fiber is not set, switch to the main fiber
        if(m_yieldToSchedulerFiber) {
            setRunningFiber(this);
            if(swapcontext(&(_scheduler_fiber->m_ctx), &m_ctx) == -1) {
                std::cerr << "resume() to _scheduler_fiber failed\n";
                pthread_exit(NULL);
            }	
        } else {
            setRunningFiber(this);
            if(swapcontext(&(_main_fiber->m_ctx), &m_ctx) == -1) {
                std::cerr << "resume() to _main_fiber failed\n";
                pthread_exit(NULL);
            }
        }
    }

    void Fiber::yield() {
        // check if the fiber is in RUNNING or TERM state
        assert(m_state == RUNNING || m_state == TERM);

        // set the state of fiber to READY if in RUNNING state, otherwise keep it in TERM state
        if (m_state != TERM) m_state = READY;

        // switch from this fiber to the scheduler fiber if a scheduler fiber is set
        // switch from this fiber to the main fiber if a scheduler fiber is not set
        if(m_yieldToSchedulerFiber) {
            setRunningFiber(_scheduler_fiber);
            if(swapcontext(&m_ctx, &(_scheduler_fiber->m_ctx)) == -1) {
                std::cerr << "yield() to to _scheduler_fiber failed\n";
                pthread_exit(NULL);
            }		
        } else {
            setRunningFiber(_main_fiber.get());
            if(swapcontext(&m_ctx, &(_main_fiber->m_ctx)) == -1) {
                std::cerr << "yield() to _main_fiber failed\n";
                pthread_exit(NULL);
            }	
        }	
    }

    uint64_t Fiber::getId() const {
        return m_id;
    }

	Fiber::State Fiber::getState() const {
        return m_state;
    }

    void Fiber::setRunningFiber(Fiber *f) {
        _running_fiber = f;
    }

    void Fiber::setSchedulerFiber(Fiber* f) {
        _scheduler_fiber = f;
    }

    std::shared_ptr<Fiber> Fiber::getRunningFiber() {
        // if there is a running fiber, return a shared pointer to that fiber
        if(_running_fiber) return _running_fiber->shared_from_this();

        // create a main fiber/scheduler fiber
        std::shared_ptr<Fiber> main_fiber(new Fiber());
        _main_fiber = main_fiber;
        _scheduler_fiber = main_fiber.get();

        // return a shared pointer to the running fiber
        assert(_running_fiber == main_fiber.get());
        return _running_fiber->shared_from_this();
    }

    uint64_t Fiber::getFiberId() {
        if(_running_fiber) return _running_fiber->getId();
        return (uint64_t)-1;    // return -1 if there is no running fiber
    }

    void Fiber::MainFunc() {
        // get the running fiber and assert that it is not null
        std::shared_ptr<Fiber> runningFiber = getRunningFiber();
        assert(runningFiber != nullptr);

        // execute the function that the fiber needs to run, set the function pointer to null and update the state of fiber to TERM
        runningFiber->m_cb(); 
        runningFiber->m_cb = nullptr;
        runningFiber->m_state = TERM;

        // yield control back to the scheduler fiber if a scheduler fiber is set, otherwise yield control back to the main fiber
        auto raw_ptr = runningFiber.get();
        runningFiber.reset();  // release the shared pointer to the running fiber
        raw_ptr->yield(); 
    }

    Fiber::Fiber() {
        // set the running fiber to this fiber and initialize the state of fiber to RUNNING
        setRunningFiber(this);
        m_state = RUNNING;

        // initialize the execution context of main fiber
        if(getcontext(&m_ctx) == -1) {
            std::cerr << "Fiber() failed\n";
            pthread_exit(NULL);
        }

        // assign a unique id to the main fiber and increment the global fiber count
        m_id = _fiber_id++;
        _fiber_count ++;

        // output debug information about the creation of main fiber
        if(debug) std::cout << "Fiber(): main id = " << m_id << std::endl;
    }
};