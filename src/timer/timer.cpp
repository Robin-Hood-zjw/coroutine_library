#include "timer.h"

namespace sylar {
    bool Timer::cancel()
    {
        std::unique_lock<std::shared_mutex> write_lock(m_manager->m_mutex);
        if (!m_cb) return false;

        m_cb = nullptr;
        auto itr = m_manager->m_timers.find(shared_from_this());

        if (itr != m_manager->m_timers.end())
        {
            m_manager->m_timers.erase(itr);
        }

        return true;
    }

    bool Timer::refresh()
    {
        std::unique_lock<std::shared_mutex> write_lock(m_manager->m_mutex);
        if (!m_cb) return false;

        auto itr = m_manager->m_timers.find(shared_from_this());
        if (itr == m_manager->m_timers.end()) return false;

        m_manager->m_timers.erase(itr);
        m_next = std::chrono::system_clock::now() + std::chrono::milliseconds(m_ms);
        m_manager->m_timers.insert(shared_from_this());

        return true;
    }

    bool Timer::reset(uint64_t ms, bool from_now)
    {
        if (ms == m_ms && !from_now) return true;

        {
            std::unique_lock<std::shared_mutex> write_lock(m_manager->m_mutex);
            if (!m_cb) return false;

            auto itr = m_manager->m_timers.find(shared_from_this());
            if (itr == m_manager->m_timers.end()) return false;

            m_manager->m_timers.erase(itr);
        }

        auto start = from_now ?
            std::chrono::system_clock::now() :
            m_next - std::chrono::milliseconds(m_ms);

        m_ms = ms;
        m_next = start + std::chrono::milliseconds(m_ms);
        m_manager->addTimer(shared_from_this());

        return true;
    }

    Timer::Timer(uint64_t ms, std::function<void()> cb, bool recurring, TimerManager* manager):
    m_ms(ms), m_cb(cb), m_recurring(recurring), m_manager(manager)
    {
        auto now = std::chrono::system_clock::now();
        m_next = now + std::chrono::milliseconds(m_ms);
    }

    bool Timer::Comparator::operator()(const std::shared_ptr<Timer>& lhs, const std::shared_ptr<Timer>& rhs) const
    {
        assert(lhs && rhs);
        return lhs->m_next < rhs->m_next;
    }

    TimerManager::TimerManager()
    {
        m_prevTime = std::chrono::system_clock::now();
    }

    TimerManager::~TimerManager()
    {}

    std::shared_ptr<Timer> TimerManager::addTimer(uint64_t ms, std::function<void()> cb, bool recurring = false)
    {
        std::shared_ptr<Timer> timer(new Timer(ms, cb, recurring, this));
        addTimer(timer);

        return timer;
    }

    std::shared_ptr<Timer> TimerManager::addConditionTimer(uint64_t ms, std::function<void()> cb, std::weak_ptr<void> weak_cond, bool recurring = false)
    {
        auto cond_cb = [weak_cond, cb](){
            if (auto tmp = weak_cond.lock()) cb();
        };

        return addTimer(ms, cond_cb, recurring);
    }

    bool TimerManager::hasTimer()
    {
        std::shared_lock<std::shared_mutex> read_lock(m_mutex);
        return !m_timers.empty();
    }

    uint64_t TimerManager::getNextTimer()
    {
        std::shared_lock<std::shared_mutex> read_lock(m_mutex);

        m_bTicked = false;
        if (m_timers.empty()) return ~0ull;

        auto now = std::chrono::system_clock::now();
        auto time = (*m_timers.begin())->m_next;

        if (now >= time) return 0;

        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(time - now);
        return static_cast<uint64_t>(duration.count());
    }

    void TimerManager::listAllExpiredCb(std::vector<std::function<void()>>& cbs)
    {
        auto now = std::chrono::system_clock::now();
        std::unique_lock<std::shared_mutex> write_lock(m_mutex);
        bool bRollover = detectClockRollover();

        while (!m_timers.empty() && bRollover || !m_timers.empty() && (*m_timers.begin())->m_next <= now)
        {
            std::shared_ptr<Timer> temp = *m_timers.begin();
            m_timers.erase(m_timers.begin());
            cbs.emplace_back(temp->m_cb);

            if (temp->m_recurring)
            {
                temp->m_next = now + std::chrono::milliseconds(temp->m_ms);
                m_timers.insert(temp);
            }
            else
            {
                temp->m_cb = nullptr;
            }
        }
        
    }

    void TimerManager::addTimer(std::shared_ptr<Timer> timer)
    {
        bool bAtFront = false;

        {
            std::unique_lock<std::shared_mutex> write_lock(m_mutex);
            auto itr = m_timers.insert(timer).first;
            bAtFront = itr == m_timers.begin() && !m_bTicked;

            if (bAtFront) m_bTicked = true;
        }

        if (bAtFront) onTimerInsertedAtFront();
    }

    bool TimerManager::detectClockRollover()
    {
        bool bRollover = false;
        auto now = std::chrono::system_clock::now();
        auto gap = std::chrono::milliseconds(60 * 60 * 1000);

        if (now < (m_prevTime - gap)) bRollover = true;
        m_prevTime = now;

        return bRollover;
    }
}