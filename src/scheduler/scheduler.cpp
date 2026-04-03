#include "scheduler.h"

static bool debug = false;

namespace sylar {
    static thread_local Scheduler* _scheduler = nullptr;    // a thread-local variable that points to the scheduler instance associated with the current thread

    /**
     * @brief Get the This object
     */
    void Scheduler::SetThis() {
        _scheduler = this;
    }

    /**
     * @brief 
     */
    void Scheduler::tickle() {}

    /**
     * @brief a method to retrieve a task (coroutine/callback) from the task queue and executes it
     * @throw This function serves as the main loop for the thread; it should handle exceptions to prevent the entire thread pool from crashing.
     * @note 1. enable system function interception via `set_hook_enable(true)` to achieve asynchronous I/O
     * @note 2. all `resume()` calls transfer the CPU register context from the currently scheduled coroutine to the target task coroutine
     */
    void Scheduler::run() {
        int threadID = Thread::GetThreadId();
        if (debug) std::cout << "Schedule::run() starts in thread: " << threadID << std::endl;

        // intercept blocking system calls and convert them into coroutine yields
        set_hook_enable(true);

        // set the current scheduler instance as the thread-local scheduler for the current thread
        SetThis();

        // initializes a main coroutine for the current thread as the landing spot when other coroutines yield
        if (threadID != _rootThread) Fiber::GetThis();

        ScheduleTask task;
        // create an idle fiber that runs the idle function, which will yield control back to the scheduler
        std::shared_ptr<Fiber> idleFiber = std::make_shared<Fiber>(std::bind(&Scheduler::idle, this));

        while (true) {
            task.reset();
            bool wakeSignal = false;

            // iterate through the task queue to find a task to execute
            {
                std::lock_guard<std::mutex> lock(_mutex);

                auto itr = _tasks.begin();
                while (itr != _tasks.end()) {
                    // skip the task if it is assigned to a thread and the current thread is not that thread
                    if (itr->thread != -1 && itr->thread != threadID) {
                        wakeSignal = true; // tickle other threads to execute this task
                        itr++;
                        continue;
                    }

                    // check if the task has a pointer to Fiber or a callback function
                    assert(itr->fiber || itr->callback);

                    // remove the task from the task queue and increase the count of active threads
                    task = *itr;
                    _tasks.erase(itr);
                    _activeThreadCnt++;
                    break;
                }

                // signal other threads to wake up if there are still tasks in the queue
                wakeSignal = wakeSignal || itr != _tasks.end();
            }

            // wake up other threads to execute tasks if needed
            if (wakeSignal) tickle();

            // execute the task
            // 1 execute a task with a pointer to Fiber
            // 2 execute a task with a callback function
            // 3 execute the idle task when there are no tasks to run
            if(task.fiber) {
                {					
                    std::lock_guard<std::mutex> lock(task.fiber->_mutex);
                    if(task.fiber->getState() != Fiber::TERM) task.fiber->resume();
                }
                _activeThreadCnt--;
                task.reset();
            } else if (task.callback) {
                // why create a new fiber for the callback function instead of executing it directly? 
                // 1. to unify the execution model of tasks, as both fiber tasks and callback tasks will be executed in a fiber context, which allows for better scheduling and management of tasks within the scheduler.
                // 2. to allow the callback function to yield and be rescheduled by the scheduler, which is not possible if the callback function is executed directly in the thread context.
                std::shared_ptr<Fiber> fiberCallback = std::make_shared<Fiber>(task.callback); 
                {
                    std::lock_guard<std::mutex> lock(fiberCallback->_mutex);
                    fiberCallback->resume();			
                }
                _activeThreadCnt--;
                task.reset();	
            } else {
                // 系统关闭 -> idle协程将从死循环跳出并结束 -> 此时的idle协程状态为TERM -> 再次进入将跳出循环并退出run()

                // resume the idle fiber and yield control back to the scheduler until the idle fiber is terminated
                if (idleFiber->getState() == Fiber::TERM) {
                    if(debug) std::cout << "Schedule::run() ends in thread: " << threadID << std::endl;
                    break;
                }

                _idleThreadCnt++;
                idleFiber->resume();				
                _idleThreadCnt--;
            }
        }
        
    }

    /**
     * @brief a method to yield the execution of the scheduler fiber
     */
    void Scheduler::idle() {
        while(!stopping()) {
            if (debug) std::cout << "Scheduler::idle(), sleeping in thread: " << Thread::GetThreadId() << std::endl;	

            sleep(1);	
            Fiber::GetThis()->yield();
        }
    }

    /**
     * @brief a method to determine whether the scheduler can be stopped
     * @return `True`: indicates that the scheduler has completed all the tasks and is ready to exit.
     * @return `False`: indicates that tasks are running or that a stop command has not been received.
     */
    bool Scheduler::stopping() {
        std::lock_guard<std::mutex> lock(_mutex);

        return _stopping && _tasks.empty() && _activeThreadCnt == 0;
    }

    /**
     * @brief a method to check whether there are idle threads in the scheduler
      * @return `True`: indicates that there are idle threads available to execute tasks
      * @return `False`: indicates that there are no idle threads available
     */
    bool Scheduler::hasIdleThreads() {
        return _activeThreadCnt > 0;
    }

    /**
     * @brief the constructor of the Scheduler class
     * @param threads[in] The total number of physical threads in the thread pool (including the caller thread if use_caller is true).
     * @param use_caller If true, the thread will be used as a worker thread; if false, only separate worker threads are spawned.
     * @param name The identification name of the scheduler, for debugging and thread naming.
     */
    Scheduler::Scheduler(size_t threads = 1, bool use_caller = true, const std::string& name="Scheduler"):
        _useCaller(use_caller), _name(name) {
            // check the number of threads should be over 0 and the caller thread should not be associated with a scheduler
            assert(threads > 0 && Scheduler::GetThis() == nullptr);

            // set the thread-local pointer to this scheduler instance
            SetThis();

            // set the current thread's name for better debugging in 'top' or 'gdb'
            Thread::SetName(_name);

            if (use_caller) {
                // If the caller thread is used as a worker, we need one fewer 'external' thread to be spawned.
                threads--;
                // initializes the main fiber for the current thread as the landing spot for context switches
                Fiber::GetThis();

                // create a scheduler fiber that runs the scheduler's main loop
                _schedulerFiber.reset(new Fiber(std::bind(&Scheduler::run, this), 0, false)); // false -> 该调度协程退出后将返回主协程
                // set the scheduler fiber as the main fiber for the current thread
                Fiber::SetSchedulerFiber(_schedulerFiber.get());

                // store the thread ID 
                _rootThread = Thread::GetThreadId();
                _threadIds.push_back(_rootThread);
            }

            // store the number of threads in the scheduler
            _threadCnt = threads;
            if (debug) std::cout << "Scheduler::Scheduler() success" << std::endl;
        }

    /**
     * @brief the destructor of the Scheduler class
     */
    Scheduler::~Scheduler() {
        // check if the scheduler is stopping
        assert(stopping() == true);

        // reset the thread-local pointer to the scheduler instance if it points to this instance
        if (GetThis() == this) _scheduler = nullptr;

        if (debug) std::cout << "Scheduler::~Scheduler() success" << std::endl;
    }

    /**
     * @brief a method to get the name of the scheduler
     */
    const std::string& Scheduler::getName() const {
        return _name;
    }

    /**
     * @brief a method to get the currently running scheduler instance
     * @return `Scheduler*`: a pointer to the currently running scheduler instance. If there is no scheduler associated with the current thread, it returns `nullptr`.
     */
    Scheduler* Scheduler::GetThis() {
        return _scheduler;
    }

    /**
     * @brief a method to add a task (coroutine or callback) to the scheduler's task queue
     * @param fc The task to be scheduled, which can be either a pointer to a Fiber or a callback function.
     * @param thread The ID of the thread to which the task is assigned. If -1, the task can be executed by any thread in the scheduler; otherwise, it will only be executed by the specified thread.
     */
    template <class FiberOrCallback>
    void Scheduler::scheduleLock(FiberOrCallback fc, int thread = -1) {                                              
        bool wakeSignal = false;

        {
            std::lock_guard<std::mutex> lock(_mutex);
            // empty ->  all thread is idle -> need to be waken up
            wakeSignal = _tasks.empty();

            ScheduleTask task(fc, thread);
            if (task.fiber || task.callback) _tasks.push_back(task);
        }

        if (wakeSignal) tickle();
    }

    /**
     * @brief a method to start the scheduler by creating worker threads and running the scheduler's main loop on each thread
     */
    void Scheduler::start() {
        std::lock_guard<std::mutex> lock(_mutex);

        if (_stopping) {
            std::cerr << "Scheduler is stopped" << std::endl;
            return;
        }

        // check if 
        assert(_threads.empty());

        _threads.resize(_threadCnt);
        for(size_t i = 0; i < _threadCnt; i++) {
            _threads[i].reset(new Thread(std::bind(&Scheduler::run, this), _name + "_" + std::to_string(i)));
            _threadIds.push_back(_threads[i]->getId());
        }
        if (debug) std::cout << "Scheduler::start() success" << std::endl;
    }

    /**
     * @brief a method to stop the scheduler by signaling all worker threads to exit and waiting for them to finish
     */
    void Scheduler::stop() {
        if (debug) std::cout << "Schedule::stop() starts in thread: " << Thread::GetThreadId() << std::endl;
        if (stopping()) return;

        _stopping = true;

        if (_useCaller) {
            assert(GetThis() == this);
        } else {
            assert(GetThis() != this);
        }

        // wake up all worker threads to let them exit
        for (size_t i = 0; i < _threadCnt; i++) tickle();

        // wake the scheduler fiber to let it exit if the caller thread is used as a worker
        if (_schedulerFiber) tickle();

        // resume the scheduler fiber to let it exit if the caller thread is used as a worker
        if (_schedulerFiber) {
            _schedulerFiber->resume();
            if (debug) std::cout << "m_schedulerFiber ends in thread:" << Thread::GetThreadId() << std::endl;
        }

        // create a vector to hold the worker threads
        std::vector<std::shared_ptr<Thread>> vec;
        {
            std::lock_guard<std::mutex> lock(_mutex);
            vec.swap(_threads);
        }

        for (auto &i : vec) i->join();
        if (debug) std::cout << "Schedule::stop() ends in thread:" << Thread::GetThreadId() << std::endl;
    }
}