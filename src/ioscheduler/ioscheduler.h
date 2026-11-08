#ifndef __SYLAR_IOMANAGER_H__
#define __SYLAR_IOMANAGER_H__

#include "../timer/timer.h"
#include "../scheduler/scheduler.h"

namespace sylar {

// workflow
// 1. register one event
// 2. wait for it to ready
// 3. schedule the callback
// 4. unregister the event
// 5. run the callback

    class IOManager : public Scheduler, public TimerManager 
    {
    public:
        enum Event 
        {
            NONE = 0x0,
            READ = 0x1, // read -> epollin
            WRITE = 0x4 // write -> epollout
        };

        IOManager(size_t threads = 1, bool use_caller = true, const std::string &name = "IOManager");
        ~IOManager();

        // add one event at a time
        int addEvent(int fd, Event event, std::function<void()> cb = nullptr);
        // delete event
        bool delEvent(int fd, Event event);
        // delete the event and trigger its callback
        bool cancelEvent(int fd, Event event);
        // delete all events and trigger its callback
        bool cancelAll(int fd);

        static IOManager* GetThis();

    protected:
        void tickle() override;
        
        bool stopping() override;
        
        void idle() override;

        void onTimerInsertedAtFront() override;

        void contextResize(size_t size);

    private:

        struct FdContext 
        {
            struct EventContext 
            {
                Scheduler *scheduler = nullptr;     // scheduler
                std::shared_ptr<Fiber> fiber;       // callback fiber
                std::function<void()> cb;           // callback function
            };

            EventContext read;                      // read event context
            EventContext write;                     // write event context
            int fd = 0;
            Event events = NONE;                    // events registered
            std::mutex mutex;

            EventContext& getEventContext(Event event);
            void resetEventContext(EventContext &ctx);
            void triggerEvent(Event event);        
        };


        int m_epfd = 0;
        int m_tickleFds[2];                             // fd[0] -> read，fd[1] -> write
        std::atomic<size_t> m_pendingEventCount = {0};
        std::shared_mutex m_mutex;
        std::vector<FdContext *> m_fdContexts;          // store fdcontexts for each fd
    };

}

#endif