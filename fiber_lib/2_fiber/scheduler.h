#include "fiber.h"

#include <vector>

namespace sylar {
    class Scheduler {
    private:
        std::vector<std::shared_ptr<Fiber>> _tasks;

    public:
        void schedule(std::shared_ptr<Fiber> task);
        void run();
    };
}