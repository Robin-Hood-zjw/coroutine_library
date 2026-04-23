#ifndef _SCHEDULER_H_
#define _SCHEDULER_H_

#include "../hook/hook.h"
#include "../fiber/fiber.h"
#include "../thread/thread.h"

#include <mutex>
#include <vector>

namespace sylar {
    class Scheduler {
    private:
        struct ScheduleTask {
            int thread;                                         // the ID of the thread that the task is scheduled, -1 means any thread
            std::function<void()> callback;                     // a callback function that represents a new task to be scheduled
            std::shared_ptr<Fiber> fiber;                       // a scheduled existing fiber or a new fiber created from a callback function

            // default constructor to create an empty task
            ScheduleTask() {
                thread = -1;
                callback = nullptr;
                fiber = nullptr;
            }

            // constructor to create a task from a fiber or a callback and the thread ID
            ScheduleTask(std::shared_ptr<Fiber> f, int thr) {
                thread = thr;
                fiber = f;
            }

            // constructor to create a task from a pointer to a fiber or a callback and the thread ID
            ScheduleTask(std::shared_ptr<Fiber>* f, int thr) {
                thread = thr;
                fiber.swap(*f);
            }

            // constructor to create a task from a callback function and the thread ID
            ScheduleTask(std::function<void()> f, int thr) {
                thread = thr;
                callback = f;
            }		

            // constructor to create a task from a pointer to a callback function and the thread ID
            ScheduleTask(std::function<void()>* f, int thr) {
                thread = thr;
                callback.swap(*f);
            }

            // reset the task to an empty state, used for clearing the task after it is executed
            void reset() {
                thread = -1;
                callback = nullptr;
                fiber = nullptr;
            }
        };

        std::string _name;                                      // the scheduler name
        bool _useCaller;                                        // a sign that indicates whether the root thread is included in the scheduler
        bool _stopping = false;                                 // a sign that indicates whether to accept new tasks

        std::vector<int> _threadIds;                            // a vector of thread IDs corresponding to the physical threads
        std::vector<std::shared_ptr<Thread>> _pool;             // a thread pool

        int _rootThread = -1;                                   // the ID of the root thread
        std::shared_ptr<Fiber> _schedulerFiber;                 // the scheduler fiber on the root thread

        std::mutex _mutex;                                      // a mutex to protect the tasks
        std::vector<ScheduleTask> _tasks;                       // a task queue (FIFO)

        size_t _threadCnt = 0;                                  // the count of threads in the scheduler
        std::atomic<size_t> _idleThreadCnt = {0};               // the number of idle threads in the thread pool
        std::atomic<size_t> _activeThreadCnt = {0};             // the number of active threads in the thread pool

    protected:
        /**
         * @brief Set the currently running scheduler for the current thread
         * 
         * @note 1. access the current scheduler via Scheduler::GetThis()
         */
        void setRunningScheduler();

        /**
         * @brief Wake up worker threads when new tasks are added to the scheduler
         * 
         * @note 1. called after a new task is enqueued to an empty task queue
         */
        virtual void tickle();
        /**
         * @brief Run the scheduler's main loop that continuously schedules new tasks
         * 
         * @note 1. the entry point for each worker thread, repeating as below:
         * @note - fetching tasks from the task queue 
         * @note - executing fibers or callbacks
         * @note - yielding to idle() when no tasks are available
         * @note 2. the loop continues until stopping() returns true and all tasks are processed
         */
        virtual void run();
        /**
         * @brief Yield the execution of the scheduler fiber when no tasks are available
         * 
         * @note 1. called by run() when the task queue is empty, covers as below:
         * @note - put the current thread into an idle state
         * @note - increment the count of the idle threads in the thread pool
         * @note - yields the current fiber to wait for new tasks
         */
        virtual void idle();
        /**
         * @brief Check whether the scheduler is stopping
         * 
         * @return `true` if the scheduler can safely stop, `false` otherwise
         * 
         * @note 1. return `true` when the following conditions are met:
         * @note- stop() has been called AND
         * @note- No tasks remain in the queue AND
         * @note- All active fibers have completed execution
         */
        virtual bool stopping();

        /**
         * @brief Check whether there are idle threads in the scheduler
         * 
         * @return `true` if at least one idle thread exists, `false` otherwise
         * 
         * @note 1. decide whether to wake up idle threads when new tasks arrive
         */
        bool hasIdleThreads();

    public:
        /**
         * @brief Construct a new Scheduler object
         * 
         * @param threads the number of worker threads to create (excluding the caller thread if `use_caller` true)
         * @param use_caller Whether to include the calling thread as a worker thread
         *        - true: The calling thread will also execute tasks (total workers = threads + 1)
         *        - false: The calling thread only manages, not executes tasks (total workers = threads)
         * @param name the name identifier for this scheduler (for debugging & logging)
         */
        Scheduler(size_t threads=1, bool use_caller=true, const std::string& name="Scheduler");
        /**
         * @brief Destroy the Scheduler object
         * 
         * @note 1. call stop() to ensure all worker threads are properly terminated
         * @note 2. all resources are cleaned up before destruction
         */
        virtual ~Scheduler();

        /**
         * @brief Get the name of the scheduler
         * 
         * @return `const std::string&`: a reference to string
         */
        const std::string& getName() const;

        /**
         * @brief Get the currently running scheduler for the current thread
         * 
         * @note 1. `static` ensures that the retrieved object is associated with the current thread via TLS
         * 
         * @return `Scheduler*`: a pointer to the current thread's scheduler, or nullptr if none exists
         */
        static Scheduler* GetThis();

        /**
         * @brief Schedule a new task (fiber or callback) to the scheduler
         * 
         * @tparam FiberOrCallback: either Fiber pointer or function callback
         * @param fc the task to schedule (fiber or callback function)
         * @param thread target thread ID to run this task on (-1 means any available thread)
         * 
         * @note 1. The task is added to the FIFO task queue.
         * @note 2. If the queue was previously empty, tickle() is called to wake up idle worker threads.
         */
        template <class FiberOrCallback>
        void scheduleLock(FiberOrCallback fc, int thread=-1);

        /**
         * @brief Start the thread pool and begin task execution
         * 
         * @note create all worker threads and start the scheduler's main loop
         * @note if `_useCaller` is `true`, the caller thread becomes a worker thread by switching to the scheduler fiber
         */
        virtual void start();
        /**
         * @brief Stop the scheduler and terminate all worker threads
         * 
         * @note 1. shut down the scheduler through a multi-step process:
         * @note - set the stopping flag to prevent new tasks from being accepted
         * @note - wait for all pending tasks to complete
         * @note - wake up all idle threads so they can exit
         * @note - join all worker threads
         * @note 2. This method blocks until all threads have terminated.
         */
        virtual void stop();
    };
};

#endif