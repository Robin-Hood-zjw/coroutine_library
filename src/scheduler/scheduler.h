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

        int _rootThread = -1;                                   // the ID of the root thread
        size_t _threadCnt = 0;                                  // the number of threads in the scheduler

        bool _useCaller;                                        // a sign that indicates whether the caller thread is included in the scheduler
        bool _stopping = false;                                 // a sign that indicates whether the scheduler is stopping

        std::mutex _mutex;                                      // a mutex to protect the shared data of scheduler
        std::string _name;                                      // the name of the scheduler

        std::vector<int> _threadIds;                            // a vector of thread IDs corresponding to the physical threads
        std::vector<ScheduleTask> _tasks;                       // a task queue (FIFO) that holds the tasks to be scheduled
        std::vector<std::shared_ptr<Thread>> _threads;          // a vector of sylar::Thread objects that represent the physical threads

        std::shared_ptr<Fiber> _schedulerFiber;                 // the fiber that runs the scheduler's main loop, responsible for scheduling tasks and managing worker threads
        std::atomic<size_t> _idleThreadCnt = {0};               // the number of idle threads in the scheduler, used to determine whether to tickle worker threads when new tasks are added
        std::atomic<size_t> _activeThreadCnt = {0};             // the number of active threads in the scheduler, used for monitoring and debugging purposes

    protected:
        void SetThis();                                         // set the currently running scheduler

        virtual void tickle();                                  // wake the worker threads up when new tasks are added to the scheduler
        virtual void run();                                     // runs the scheduler's main loop that continuously schedules new tasks and manages the execution of tasks on worker threads
        virtual void idle();                                    // yield the execution of the scheduler fiber to allow worker threads to run when there are no scheduled tasks
        virtual bool stopping();                                // check whether the scheduler is stopping, used to determine whether to exit the scheduler's main loop

        bool hasIdleThreads();                                  // check whether there are idle threads in the scheduler

    public:
        Scheduler(size_t threads = 1, bool use_caller = true, const std::string& name="Scheduler");     // the constructor of scheduler
        virtual ~Scheduler();                                                                           // the destructor of scheduler
        const std::string& getName() const;                                                             // get the name of the scheduler
        static Scheduler* GetThis();                                                                    // get the running scheduler

        template <class FiberOrCallback>
        // schedule a new task(fiber or callback) to the scheduler
        void scheduleLock(FiberOrCallback fc, int thread = -1);

        virtual void start();                                                                           // start the thread pool
        virtual void stop();                                                                            // stop the thread pool
    };
};

#endif