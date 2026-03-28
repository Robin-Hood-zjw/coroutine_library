#include "scheduler.h"

namespace sylar {
    void Scheduler::schedule(std::shared_ptr<Fiber> task) {
        _tasks.push_back(task);
    }

    void Scheduler::run() {
        std::cout << " number " << _tasks.size() << std::endl;

        std::shared_ptr<Fiber> task;
        auto it = _tasks.begin();

        while (it != _tasks.end()) {
            task = *it;
            task->resume();
            it++;
        }

        _tasks.clear();
    }
}