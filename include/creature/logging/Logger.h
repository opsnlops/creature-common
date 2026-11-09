
#pragma once

#include <memory>
#include <string>

#include <fmt/format.h>

namespace creatures {

/**
 * Abstract out the logger to an Interface
 *
 * This is used to abstract out the logger to allow for different
 * implementations, to to allow for unit testing without needed to depend on the
 * global namespace.
 */
class Logger {

  public:
    virtual ~Logger() = default;

    // A chance to initialize the logger as needed
    virtual void init(std::string loggerName) = 0;

    // Set the minimum severity that will be emitted. Accepts the standard
    // level names (trace, debug, info, warn/warning, error, critical, off).
    // Implementations are expected to apply this process-wide so every logger
    // shares the same level.
    virtual void setLevel(const std::string &levelName) = 0;

    template <typename... Args> void trace(const std::string &format, Args &&...args) {
        auto format_args = fmt::make_format_args(args...);
        logTrace(format, format_args);
    }

    template <typename... Args> void debug(const std::string &format, Args &&...args) {
        auto format_args = fmt::make_format_args(args...);
        logDebug(format, format_args);
    }

    template <typename... Args> void info(const std::string &format, Args &&...args) {
        auto format_args = fmt::make_format_args(args...);
        logInfo(format, format_args);
    }

    template <typename... Args> void warn(const std::string &format, Args &&...args) {
        auto format_args = fmt::make_format_args(args...);
        logWarning(format, format_args);
    }

    template <typename... Args> void error(const std::string &format, Args &&...args) {
        auto format_args = fmt::make_format_args(args...);
        logError(format, format_args);
    }

    template <typename... Args> void critical(const std::string &format, Args &&...args) {
        auto format_args = fmt::make_format_args(args...);
        logCritical(format, format_args);
    }

  protected:
    virtual void logTrace(const std::string &format, fmt::format_args args) = 0;
    virtual void logDebug(const std::string &format, fmt::format_args args) = 0;
    virtual void logInfo(const std::string &format, fmt::format_args args) = 0;
    virtual void logWarning(const std::string &format, fmt::format_args args) = 0;
    virtual void logError(const std::string &format, fmt::format_args args) = 0;
    virtual void logCritical(const std::string &format, fmt::format_args args) = 0;
};

} // namespace creatures