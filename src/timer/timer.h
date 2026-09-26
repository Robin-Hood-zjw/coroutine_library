#ifndef __SYLAR_TIMER_H__
#define __SYLAR_TIMER_H__

#include <set>
#include <mutex>
#include <memory>
#include <vector>
#include <assert.h>
#include <functional>
#include <shared_mutex>

namespace sylar {
    class TimerManager;

    class Timer : public std::enable_shared_from_this<Timer> {
    friend class TimerManager;

    public:
        bool cancel();                                                          // remove from timer manager
        bool refresh();                                                         // refresh timer in timer manager
        bool reset(uint64_t ms, bool from_now);                                 // reset timer with new timeout time

    private:
        uint64_t _ms = 0;                                                       // a sign to indicate the timeout time in milliseconds
        bool _recurring = false;                                                // a sign to indicate whether the timer is recurring

        TimerManager* _manager = nullptr;                                       // a pointer to the timer manager that manages this timer

        std::function<void()> _callback;                                        // a callback function when the timer expires
        std::chrono::time_point<std::chrono::system_clock> _next;               // a sign to indicate the next timeout time point

        Timer(uint64_t ms, std::function<void()> cb, bool recurring, TimerManager* manager);

        // a comparator to compare two timers, used for sorting timers in the timer manager
        struct Comparator {
            bool operator()(const std::shared_ptr<Timer>& lhs, const std::shared_ptr<Timer>& rhs) const;
        };
    };

    class TimerManager {
    friend class Timer;

    public:
        TimerManager();
        virtual ~TimerManager();

        std::shared_ptr<Timer> addTimer(uint64_t ms, std::function<void()> cb, bool recurring = false);                                             // add a timer
        std::shared_ptr<Timer> addConditionTimer(uint64_t ms, std::function<void()> cb, std::weak_ptr<void> weak_cond, bool recurring = false);     // add a timer with a weak condition

        bool hasTimer();                                                        // check if there is any timer
        uint64_t getNextTimeoutTimer();                                         // get the next timeout time
        void getExpiredCallbacks(std::vector<std::function<void()>>& cbs);      // get all expired timer's callback functions

    protected:
        // 当一个最早的timer加入到堆中 -> 调用该函数
        virtual void onTimerInsertedAtFront() {};      // a function to be called when a timer is inserted at the front of the timer manager

        // 添加timer
        void addTimer(std::shared_ptr<Timer> timer);    // add a timer to the timer manager, used by Timer::refresh() and Timer::reset()
    private:
        std::shared_mutex _mutex;                                               // a mutex to protect the timer manager
        std::set<std::shared_ptr<Timer>, Timer::Comparator> _timers;            // a set to store timers, sorted by the next timeout time
        // 在下次getNextTime()执行前 onTimerInsertedAtFront()是否已经被触发了 -> 在此过程中 onTimerInsertedAtFront()只执行一次
        bool _tickled = false;                                                  // a sign to indicate whether the timer manager has been tickled
        // 上次检查系统时间是否回退的绝对时间
        std::chrono::time_point<std::chrono::system_clock> _previouseTime;     // a sign to indicate the previous time point when checking for system clock rollback

        // 当系统时间改变时 -> 调用该函数
        bool detectClockRollover();                     // a function to detect whether the system clock has rolled over, used for handling system clock rollback
    };
}

#endif