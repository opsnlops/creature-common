#pragma once

#include <cstddef>
#include <string>

namespace creatures {

/**
 * The longest thread name this platform accepts, not counting the terminator
 *
 * Linux allows 15 bytes. Longer names make pthread_setname_np() fail with
 * ERANGE, leaving the thread with its parent's name. macOS allows 63.
 */
#if defined(__linux__)
inline constexpr std::size_t MAX_THREAD_NAME_LENGTH = 15;
#elif defined(__APPLE__)
inline constexpr std::size_t MAX_THREAD_NAME_LENGTH = 63;
#else
inline constexpr std::size_t MAX_THREAD_NAME_LENGTH = 0;
#endif

/**
 * The name a thread actually gets: shortened to what the platform allows
 */
std::string fitThreadName(const std::string &name);

/**
 * Name the calling thread, as it shows in htop, top -H, gdb, and Instruments
 *
 * Names longer than the platform allows are shortened rather than rejected, so
 * a long name still labels the thread. Elsewhere than Linux and macOS this does
 * nothing.
 */
void setThreadName(const std::string &name);

} // namespace creatures
