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

    class TimerManager {};
}

#endif