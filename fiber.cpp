#include "fiber.h"

static bool debug = false;

namespace sylar {
    uint64_t Fiber::getId() const { return m_id; }
	Fiber::State Fiber::getState() const { return m_state; }
};