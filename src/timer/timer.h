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

        struct Comparator {
            bool operator()(const std::shared_ptr<Timer>& lhs, const std::shared_ptr<Timer>& rhs) const;
        };

        uint64_t m_ms = 0;
        bool m_recurring = false;
        std::function<void()> m_cb;
        TimerManager* m_manager = nullptr;
        std::chrono::time_point<std::chrono::system_clock> m_next;
    };

    class TimerManager
    {
        friend class Timer;

        public:
        TimerManager();
        virtual ~TimerManager();

        std::shared_ptr<Timer> addTimer(uint64_t ms, std::function<void()> cb, bool recurring = false);
        std::shared_ptr<Timer> addConditionTimer(uint64_t ms, std::function<void()> cb, std::weak_ptr<void> weak_cond, bool recurring = false);

        bool hasTimer();
        uint64_t getNextTimer();
        void listAllExpiredCb(std::vector<std::function<void()>>& cbs);

        protected:
        virtual void onTimerInsertedAtFront();
        void addTimer(std::shared_ptr<Timer> timer);

        private:
        bool detectClockRollover();

        bool m_bTicked = false;
        std::shared_mutex m_mutex;
        std::set<std::shared_ptr<Timer>, Timer::Comparator> m_timers;
        std::chrono::time_point<std::chrono::system_clock> m_prevTime;
    };
}

#endif