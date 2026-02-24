#pragma once

#include <atomic>

/**
 * @brief Global shutdown flag so long-running handlers can abort and exit quickly.
 *
 * When SIGINT/SIGTERM is received, main sets this flag. Handlers (e.g. createInstance)
 * can check it and throw/return so the event loop can process quit() and the
 * process can exit without waiting for full timeouts or relying on SIGKILL.
 */
namespace ShutdownFlag {

/** Return true if shutdown (Ctrl+C / SIGTERM) was requested. */
bool isRequested();

/** Set the shutdown flag (called from signal handler in main). */
void setRequested();

} // namespace ShutdownFlag
