#pragma once

#include <string>

namespace mmt8 {

/**
 * Background logger. log() is safe on any thread, the audio thread included: it formats into a
 * fixed slot of a lock-free queue and never blocks or allocates; a worker thread writes the file.
 * Lines are dropped when the queue is full.
 */
void log_start(const std::string &path);
void log_stop();
void log(const char *fmt, ...)
#if defined(__MINGW32__)
    __attribute__((format(gnu_printf, 1, 2)))
#elif defined(__GNUC__)
    __attribute__((format(printf, 1, 2)))
#endif
    ;

/** Directory holding this plugin binary, with a trailing separator; empty if unknown. */
std::string module_dir();

}  // namespace mmt8
