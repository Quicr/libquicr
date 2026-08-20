#pragma once

#include "quicr/utilities/format.h"

#include <memory>
#include <source_location>
#include <string_view>
#include <version>

namespace quicr {

#define QUICR_LOG_TRACE 0
#define QUICR_LOG_DEBUG 1
#define QUICR_LOG_INFO 2
#define QUICR_LOG_WARN 3
#define QUICR_LOG_ERROR 4
#define QUICR_LOG_CRITICAL 5
#define QUICR_LOG_OFF 6

    class Logger
    {
      public:
        enum class Level
        {
            Trace = QUICR_LOG_TRACE,
            Debug = QUICR_LOG_DEBUG,
            Info = QUICR_LOG_INFO,
            Warn = QUICR_LOG_WARN,
            Error = QUICR_LOG_ERROR,
            Critical = QUICR_LOG_CRITICAL,
            Off = QUICR_LOG_OFF,
        };

        virtual ~Logger() = default;

        virtual void SetLevel(Level max_level) = 0;

        virtual void Log(Level level, std::string_view msg, std::source_location = std::source_location::current()) = 0;

        virtual bool ShouldLog(Level level) const noexcept;

        template<typename... Args>
        void Log(std::source_location location,
                 Level level,
                 std::conditional_t<sizeof...(Args) == 0, std::string_view, std_or_fmt::format_string<Args...>> msg,
                 Args&&... args)
        try {
            if (!ShouldLog(level)) {
                return;
            }

            if constexpr (sizeof...(Args) == 0) {
                Log(level, msg, location);
            } else {
                Log(level, std_or_fmt::vformat(msg.get(), std_or_fmt::make_format_args(args...)), location);
            }
        } catch (const std::exception& e) {
            Log(Level::Error, std_or_fmt::format("log failed to format (error={})", e.what()), location);
        }
    };

#define QUICR_LOGGER_LOG(logger, level, msg, ...)                                                                      \
    do {                                                                                                               \
        if (logger) {                                                                                                  \
            logger->Log(std::source_location::current(), level, msg __VA_OPT__(, ) __VA_ARGS__);                       \
        }                                                                                                              \
    } while (0)

#ifndef QUICR_ACTIVE_LOG_LEVEL
#ifdef NDEBUG
#define QUICR_ACTIVE_LOG_LEVEL QUICR_LOG_INFO
#else
#define QUICR_ACTIVE_LOG_LEVEL QUICR_LOG_DEBUG
#endif
#endif

#if QUICR_ACTIVE_LOG_LEVEL <= QUICR_LOG_TRACE
#define QUICR_LOGGER_TRACE(logger, msg, ...)                                                                           \
    QUICR_LOGGER_LOG(logger, quicr::Logger::Level::Trace, msg __VA_OPT__(, ) __VA_ARGS__)
#else
#define QUICR_LOGGER_TRACE(logger, msg, ...)
#endif
#if QUICR_ACTIVE_LOG_LEVEL <= QUICR_LOG_DEBUG
#define QUICR_LOGGER_DEBUG(logger, msg, ...)                                                                           \
    QUICR_LOGGER_LOG(logger, quicr::Logger::Level::Debug, msg __VA_OPT__(, ) __VA_ARGS__)
#else
#define QUICR_LOGGER_DEBUG(logger, msg, ...)
#endif
#if QUICR_ACTIVE_LOG_LEVEL <= QUICR_LOG_INFO
#define QUICR_LOGGER_INFO(logger, msg, ...)                                                                            \
    QUICR_LOGGER_LOG(logger, quicr::Logger::Level::Info, msg __VA_OPT__(, ) __VA_ARGS__)
#else
#define QUICR_LOGGER_INFO(logger, msg, ...)
#endif
#if QUICR_ACTIVE_LOG_LEVEL <= QUICR_LOG_WARN
#define QUICR_LOGGER_WARN(logger, msg, ...)                                                                            \
    QUICR_LOGGER_LOG(logger, quicr::Logger::Level::Warn, msg __VA_OPT__(, ) __VA_ARGS__)
#else
#define QUICR_LOGGER_WARN(logger, msg, ...)
#endif
#if QUICR_ACTIVE_LOG_LEVEL <= QUICR_LOG_ERROR
#define QUICR_LOGGER_ERROR(logger, msg, ...)                                                                           \
    QUICR_LOGGER_LOG(logger, quicr::Logger::Level::Error, msg __VA_OPT__(, ) __VA_ARGS__)
#else
#define QUICR_LOGGER_ERROR(logger, msg, ...)
#endif
#if QUICR_ACTIVE_LOG_LEVEL <= QUICR_LOG_CRITICAL
#define QUICR_LOGGER_CRITICAL(logger, msg, ...)                                                                        \
    QUICR_LOGGER_LOG(logger, quicr::Logger::Level::Critical, msg __VA_OPT__(, ) __VA_ARGS__)
#else
#define QUICR_LOGGER_CRITICAL(logger, msg, ...)
#endif

}
