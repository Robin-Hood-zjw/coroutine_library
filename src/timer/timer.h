#ifndef __SYLAR_TIMER_H__
#define __SYLAR_TIMER_H__

#include <set>
#include <mutex>
#include <memory>
#include <vector>
#include <functional>
#include <shared_mutex>

#include <assert.h>

namespace sylar {
    class TimerManager;

    class Timer : public std::enable_shared_from_this<Timer>
    {
        friend class TimerManager;

        public:
        // delete a timer from the timer heap
        bool cancel();

        // refresh a timer
        bool refresh();

        // reset a timeout duration of the timer
        bool reset(uint64_t ms, bool from_now);

        private:
        Timer(uint64_t ms, std::function<void()> cb, bool recurring, TimerManager* manager);

        // the comparator that works in the timer heap
        struct Comparator {
            bool operator()(const std::shared_ptr<Timer>& lhs, const std::shared_ptr<Timer>& rhs) const;
        };

        uint64_t m_ms = 0;                                         // the interval that the timer riggers
        bool m_recurring = false;                                  // a flag indicating if the timer is one-shot
        std::function<void()> m_cb;                                // the logic executed after the timeout
        TimerManager* m_manager = nullptr;                         // the TimerManager to which the timer belongs
        std::chrono::time_point<std::chrono::system_clock> m_next; // the absolute timestamp of the next timeout
    };

    class TimerManager
    {
        friend class Timer;

        public:
        TimerManager();
        virtual ~TimerManager();

        // add a timer into the timer heap
        std::shared_ptr<Timer> addTimer(uint64_t ms, std::function<void()> cb, bool recurring = false);
        // add a timer when the object pointed by the weak ptr is valid
        std::shared_ptr<Timer> addConditionTimer(uint64_t ms, std::function<void()> cb, std::weak_ptr<void> weak_cond, bool recurring = false);

        // check if the timer heap has timers
        bool hasTimer();
        // get how long the next timeout happens
        uint64_t getNextTimer();
        // identify all the expired timers, collect their callbacks, then update or remove
        void listAllExpiredCb(std::vector<std::function<void()>>& cbs);

        protected:
        virtual void onTimerInsertedAtFront();
        void addTimer(std::shared_ptr<Timer> timer);

        private:
        // detect if the system clock rolls over significantly(> 1 hour)
        bool detectClockRollover();

        bool m_bTicked = false;                                         // a flag indicating if the timeout triggers or resets
        std::shared_mutex m_mutex;                                      // a read-write lock that protects m_timers from cocurrent access
        std::set<std::shared_ptr<Timer>, Timer::Comparator> m_timers;   // a increasing-order timer heap
        std::chrono::time_point<std::chrono::system_clock> m_prevTime;  // the system timestamp when the last detectClockRollover call
    };
}

#endif