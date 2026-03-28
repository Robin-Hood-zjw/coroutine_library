#include "fiber.h"
#include "scheduler.h"

#include <vector>

using namespace sylar; 

void test_fiber(int i) {
	std::cout << "hello world " << i << std::endl;
}

int main() {
	// initialize main fiber
	Fiber::GetThis();

	// create scheduler
	Scheduler sc;

	// create 20 child fibers
	for(auto i = 0; i < 20; i++) {
		// create child fiber
		// use shared pointer to automatically manage resources -> automatically release resources created by child fiber after expiration
		// bind function -> bind function and parameters to return a function object
		std::shared_ptr<Fiber> fiber = std::make_shared<Fiber>(std::bind(test_fiber, i), 0, false);
		sc.schedule(fiber);
	}

	// execute scheduler
	sc.run();

	return 0;
}