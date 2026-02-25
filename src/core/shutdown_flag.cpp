#include "core/shutdown_flag.h"
#include <atomic>

namespace ShutdownFlag {

static std::atomic<bool> g_requested{false};

bool isRequested() { return g_requested.load(std::memory_order_relaxed); }

void setRequested() { g_requested.store(true, std::memory_order_relaxed); }

} // namespace ShutdownFlag
