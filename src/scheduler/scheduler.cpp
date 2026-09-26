#include "scheduler.h"

static bool debug = false;

namespace sylar {
    static thread_local Scheduler* _scheduler = nullptr;    // a thread-local variable that points to the scheduler instance associated with the current thread

    void Scheduler::setRunningScheduler() {
        _scheduler = this;
    }

    void Scheduler::tickle() {}

    void Scheduler::run()
    {
        int thread_id = Thread::getThreadId();
        if(debug) std::cout << "Schedule::run() starts in thread: " << thread_id << std::endl;
        
        set_hook_enable(true);

        setRunningScheduler();

        // 运行在新创建的线程 -> 需要创建主协程
        if (thread_id != m_rootThreadId) Fiber::getRunningFiber();

        std::shared_ptr<Fiber> idle_fiber = std::make_shared<Fiber>(std::bind(&Scheduler::idle, this));
        ScheduleTask task;
        
        while(true)
        {
            task.reset();
            bool tickle_me = false;

            {
                auto it = m_tasks.begin();
                std::lock_guard<std::mutex> lock(m_mutex);

                while (it != m_tasks.end())
                {
                    if (it->thread != -1 && it->thread != thread_id)
                    {
                        it++;
                        tickle_me = true;
                        continue;
                    }

                    assert(it->fiber || it->callback);
                    task = *it;
                    m_tasks.erase(it); 
                    m_activeThreadCnt++;
                    break;
                }

                tickle_me = tickle_me || (it != m_tasks.end());
            }

            if (tickle_me) tickle();

            // 3 执行任务
            if (task.fiber)
            {
                {					
                    std::lock_guard<std::mutex> lock(task.fiber->m_mutex);

                    if (task.fiber->getState()!=Fiber::TERM)
                    {
                        task.fiber->resume();	
                    }
                }

                m_activeThreadCnt--;
                task.reset();
            }
            else if (task.callback)
            {
                std::shared_ptr<Fiber> cb_fiber = std::make_shared<Fiber>(task.callback);

                {
                    std::lock_guard<std::mutex> lock(cb_fiber->m_mutex);
                    cb_fiber->resume();			
                }

                m_activeThreadCnt--;
                task.reset();	
            }
            // 4 无任务 -> 执行空闲协程
            else
            {		
                // 系统关闭 -> idle协程将从死循环跳出并结束 -> 此时的idle协程状态为TERM -> 再次进入将跳出循环并退出run()
                if (idle_fiber->getState() == Fiber::TERM) 
                {
                    if(debug) std::cout << "Schedule::run() ends in thread: " << thread_id << std::endl;
                    break;
                }

                m_idleThreadCnt++;
                idle_fiber->resume();				
                m_idleThreadCnt--;
            }
        }
        
    }

    void Scheduler::idle() {
        while(!stopping()) {
            if (debug) std::cout << "Scheduler::idle(), sleeping in thread: " << Thread::getThreadId() << std::endl;	

            sleep(1);	
            Fiber::getRunningFiber()->yield();
        }
    }

    bool Scheduler::stopping() {
        std::lock_guard<std::mutex> lock(m_mutex);

        return m_stopping && m_tasks.empty() && m_activeThreadCnt == 0;
    }

    bool Scheduler::hasIdleThreads() {
        return m_activeThreadCnt > 0;
    }

    Scheduler::Scheduler(size_t threads=1, bool use_caller=true, const std::string& name="Scheduler"):
        m_useCallerThread(use_caller), m_name(name) {
            // check the number of threads should be over 0 and the caller thread should not be associated with a scheduler
            assert(threads > 0 && Scheduler::getRunningScheduler() == nullptr);

            // set the thread-local pointer to this scheduler instance
            setRunningScheduler();

            // set the current thread's name for better debugging in 'top' or 'gdb'
            Thread::setRunningThreadName(m_name);

            if (use_caller) {
                // If the caller thread is used as a worker, we need one fewer 'external' thread to be spawned.
                threads--;
                // initializes the main fiber for the current thread as the landing spot for context switches
                Fiber::getRunningFiber();

                // create a scheduler fiber that runs the scheduler's main loop
                m_schedulerFiber.reset(new Fiber(std::bind(&Scheduler::run, this), 0, false)); // false -> 该调度协程退出后将返回主协程
                // set the scheduler fiber as the main fiber for the current thread
                Fiber::setSchedulerFiber(m_schedulerFiber.get());

                // store the thread ID 
                m_rootThreadId = Thread::getThreadId();
                m_threadIds.push_back(m_rootThreadId);
            }

            // store the number of threads in the scheduler
            m_threadCnt = threads;
            if (debug) std::cout << "Scheduler::Scheduler() success" << std::endl;
        }

    Scheduler::~Scheduler() {
        // check if the scheduler is stopping
        assert(stopping() == true);

        // reset the thread-local pointer to the scheduler instance if it points to this instance
        if (getRunningScheduler() == this) _scheduler = nullptr;

        if (debug) std::cout << "Scheduler::~Scheduler() success" << std::endl;
    }

    const std::string& Scheduler::getName() const {
        return m_name;
    }

    Scheduler* Scheduler::getRunningScheduler() {
        return _scheduler;
    }

    template <class FiberOrCallback>
    void Scheduler::scheduleLock(FiberOrCallback fc, int thread=-1) {                                              
        bool wakeSignal = false;

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            // empty ->  all thread is idle -> need to be waken up
            wakeSignal = m_tasks.empty();

            ScheduleTask task(fc, thread);
            if (task.fiber || task.callback) m_tasks.push_back(task);
        }

        if (wakeSignal) tickle();
    }

    void Scheduler::start() {
        std::lock_guard<std::mutex> lock(m_mutex);

        if (m_stopping) {
            std::cerr << "Scheduler is stopped" << std::endl;
            return;
        }

        assert(m_pool.empty());

        m_pool.resize(m_threadCnt);
        for(size_t i = 0; i < m_threadCnt; i++) {
            m_pool[i].reset(new Thread(std::bind(&Scheduler::run, this), m_name + "_" + std::to_string(i)));
            m_threadIds.push_back(m_pool[i]->getId());
        }
        if (debug) std::cout << "Scheduler::start() success" << std::endl;
    }

    void Scheduler::stop() {
        if (debug) std::cout << "Schedule::stop() starts in thread: " << Thread::getThreadId() << std::endl;
        if (stopping()) return;

        m_stopping = true;

        if (m_useCallerThread) {
            assert(getRunningScheduler() == this);
        } else {
            assert(getRunningScheduler() != this);
        }

        // wake up all worker threads to let them exit
        for (size_t i = 0; i < m_threadCnt; i++) tickle();

        // wake the scheduler fiber to let it exit if the caller thread is used as a worker
        if (m_schedulerFiber) tickle();

        // resume the scheduler fiber to let it exit if the caller thread is used as a worker
        if (m_schedulerFiber) {
            m_schedulerFiber->resume();
            if (debug) std::cout << "m_schedulerFiber ends in thread:" << Thread::getThreadId() << std::endl;
        }

        // create a vector to hold the worker threads
        std::vector<std::shared_ptr<Thread>> vec;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            vec.swap(m_pool);
        }

        for (auto &i : vec) i->join();
        if (debug) std::cout << "Schedule::stop() ends in thread:" << Thread::getThreadId() << std::endl;
    }
}