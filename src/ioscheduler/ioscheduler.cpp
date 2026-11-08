#include <sys/epoll.h>

#include "ioscheduler.h"

namespace sylar {
    IOManager* IOManager::GetThis()
    {
        return dynamic_cast<IOManager*>(Scheduler::GetThis());
    }

    IOManager::FdContext::EventContext& IOManager::FdContext::getEventContext(Event event)
    {
        assert(event == READ || event == WRITE);

        switch (event)
        {
            case READ:
                return read;
            case WRITE:
                return write;
        }

        throw std::invalid_argument("Unsupported event type");
    }

    void IOManager::FdContext::resetEventContext(EventContext& ctx)
    {
        ctx.scheduler = nullptr;
        ctx.fiber.reset();
        ctx.cb = nullptr;
    }

    void IOManager::FdContext::triggerEvent(Event event)
    {
        assert(events && event);

        // delete an event
        events = (Event)(events & ~event);

        // trigger an event
        EventContext& ctx = getEventContext(event);
        if (ctx.cb)
        {
            ctx.scheduler->scheduleLock(&ctx.cb);
        } 
        else
        {
            ctx.scheduler->scheduleLock(&ctx.fiber);
        }

        // reset the event context
        resetEventContext(ctx);
    }

    IOManager::IOManager(size_t threads, bool use_caller, const std::string& name): 
    Scheduler(threads, use_caller, name), TimerManager()
    {
        // create epoll fd
        m_epfd = epoll_create(5000);
        assert(m_epfd > 0);

        // create pipe
        int rt = pipe(m_tickleFds);
        assert(!rt);

        // add read event to epoll
        epoll_event event;
        event.events = EPOLLIN | EPOLLET;
        event.data.fd = m_tickleFds[0];

        rt = epoll_ctl(m_epfd, EPOLL_CTL_ADD, m_tickleFds[0], &event);
        assert(!rt);

        contextResize(32);
        start();
    }

    IOManager::~IOManager()
    {
        stop();
        close(m_epfd);
        close(m_tickleFds[0]);
        close(m_tickleFds[1]);

        for (size_t i = 0; i < m_fdContexts.size(); i++)
        {
            if (m_fdContexts[i])
                delete m_fdContexts[i];
        }
        
    }

    void IOManager::contextResize(size_t size)
    {
        m_fdContexts.resize(size);

        for (size_t i = 0; i < m_fdContexts.size(); i++)
        {
            if (!m_fdContexts[i])
            {
                m_fdContexts[i] = new FdContext();
                m_fdContexts[i]->fd = i;
            }
        }
        
    }

    int IOManager::addEvent(int fd, Event event, std::function<void()> cb)
    {
        FdContext* fd_ctx = nullptr;
        std::shared_lock<std::shared_mutex> read_lock(m_mutex);

        if ((int)m_fdContexts.size() > fd)
        {
            fd_ctx = m_fdContexts[fd];
            read_lock.unlock();
        }
        else
        {
            read_lock.unlock();
            std::unique_lock<std::shared_mutex> write_lock(m_mutex);

            contextResize(fd * 1.5);
            fd_ctx = m_fdContexts[fd];
        }
    }

    bool IOManager::delEvent(int fd, Event event)
    {
        FdContext* fd_ctx = nullptr;

        std::shared_lock<std::shared_mutex> read_lock(m_mutex);
        if ((int)m_fdContexts.size() > fd)
        {
            fd_ctx = m_fdContexts[fd];
            read_lock.unlock();
        }
        else
        {
            read_lock.unlock();
            return false;
        }

        std::lock_guard<std::mutex> lock(fd_ctx->mutex);

        if (!(fd->events & event))
            return false;

        Event new_events = (Event)(fd_ctx->events & ~event);
        int op = new_events ? EPOLL_CTL_MOD : EPOLL_CTL_DEL;
        epoll_event epevent;
        epevent.events = EPOLLET | new_events;
        epevent.data.ptr = fd_ctx;

        int rt = epoll_ctl(m_epfd, op, fd, &epevent);
        if (rt)
        {
            std::cerr << "delEvent:" << stderror(errno) << std::endl;
            return -1;
        }

        --m_pendingEventCount;

        FdContext::EventContext event_ctx = fd_ctx->getEventContext(event);
        fd_ctx->resetEventContext(event_ctx);
        return true;
    }

    bool IOManager::cancelEvent(int fd, Event event)
    {
        FdContext* fd_ctx = nullptr;

        std::shared_lock<std::shared_mutex> read_lock(m_mutex);
        if ((int)m_fdContexts.size() > fd)
        {
            fd_ctx = m_fdContexts[fd];
            read_lock.unlock();
        }
        else
        {
            read_lock.unlock();
            return false;
        }

        std::lock_guard<std::mutex> lock(fd_ctx->mutex);

        if (!(fd_ctx->events & event))
            return false;

        Event new_events = (Event)(fd->events & ~event);
        int op = new_events ? EPOLL_CTL_MOD : EPOLL_CTL_DEL;
        epoll_event epevent;
        epevent.events = EPOLLET | new_events;
        epevent.data.ptr = fd_ctx;

        int rt = epoll_ctl(m_epfd, fd, &epevent);
        if (rt)
        {
            std::cerr << "cancelEvent::epoll_ctl failed: " << stderror(errno) << std::endl;
            return -1;
        }

        --m_pendingEventCount;
        fd_ctx->triggerEvent(event);

        return true;
    }

    bool IOManager::cancelAll(int fd)
    {
        FdContext* fd_ctx = nullptr;

        std::shared_lock<std::shared_mutex> read_lock(m_mutex);
        if ((int)m_fdContexts.size() > fd)
        {
            fd_ctx = m_fdContexts[fd];
            read_lock.unlock();
        }
        else
        {
            read_lock.unlock();
            return false;
        }

        std::lock_guard<std::mutex> lock(fd_ctx->mutex);

        if (!fd_ctx->events)
            return false;

        int op = EPOLL_CTL_DEL;
        epoll_event epevent;
        epevent.events = 0;
        epevents.data.ptr = fd_ctx;

        int rt = epoll_ctl(m_epfd, op, fd, &epevent);
        if (rt)
        {
            std::cerr << "IOManager::epoll_ctl failed: " << stderror(errno) << std::endl;
            return -1;
        }

        if (fd_ctx->events & READ)
        {
            fd_ctx->triggerEvent(READ);
            --m_pendingEventCount;
        }

        if (fd_events & WRITE)
        {
            fd_ctx->triggerEvent(WRITE);
            --m_pendingEventCount;
        }

        assert(fd_ctx->events == 0);
        return true;
    }

    void IOManager::tickle()
    {
        if (!hasIdleThreads())
            return;

        int rt = write(m_tickleFds[1], "T", 1);
        assert(rt == 1);
    }

    bool IOManager::stopping()
    {
        uint64_t timeout = getNextTimer();
        return timeout == ~0ull && m_pendingEventCount == 0 && Scheduler::stopping();
    }

    void IOManager::idle()
    {
        static const uint64_t MAX_EVENTS = 256;
        std::unique_ptr<epoll>
    }

    void IOManager::onTimerInsertedAtFront()
    {
        tickle();
    }
}