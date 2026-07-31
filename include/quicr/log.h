#pragma once

#include <version>
#ifdef QUICR_HAVE_STD_FORMAT
#include <format>
namespace std_or_fmt = std;
#else
#include <fmt/format.h>
namespace std_or_fmt = fmt;
#endif
#include <memory>
#include <source_location>
#include <string_view>

namespace quicr {

    class Logger
    {
        Logger(std::string_view name);

      public:
        enum class Level
        {
            Trace,
            Debug,
            Info,
            Warn,
            Error,
            Critical,
        };

        static std::shared_ptr<Logger> Create(std::string_view name);

        static std::shared_ptr<Logger> GetDefault();

        void SetLevel(Level max_level);

        void Log(Level level, std::string_view msg, std::source_location = std::source_location::current());

        template<typename... Args>
        void Log(std::source_location location,
                 Level level,
                 std::conditional_t<sizeof...(Args) == 0, std::string_view, std_or_fmt::format_string<Args...>> msg,
                 Args&&... args)
        try {
            if constexpr (sizeof...(Args) == 0) {
                Log(level, msg, location);
            } else {
                Log(level, std_or_fmt::vformat(msg.get(), std_or_fmt::make_format_args(args...)), location);
            }
        } catch (const std::exception& e) {
            Log(Level::Error, std_or_fmt::format("log failed to format (error={})", e.what()), location);
        }

      private:
        class Impl;
        std::unique_ptr<Impl> impl_;
    };

#define QUICR_LOGGER_LOG(logger, level, msg, ...)                                                                      \
    logger->Log(std::source_location::current(), level, msg __VA_OPT__(, ) __VA_ARGS__)

#define QUICR_LOG(level, msg, ...) QUICR_LOGGER_LOG(quicr::Logger::GetDefault(), level, msg __VA_OPT__(, ) __VA_ARGS__)

#define QUICR_LOG_TRACE 0
#define QUICR_LOG_DEBUG 1
#define QUICR_LOG_INFO 2
#define QUICR_LOG_WARN 3
#define QUICR_LOG_ERROR 4
#define QUICR_LOG_CRITICAL 5
#define QUICR_LOG_OFF 6

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
#define QUICR_TRACE(msg, ...) QUICR_LOG(quicr::Logger::Level::Trace, msg __VA_OPT__(, ) __VA_ARGS__)
#else
#define QUICR_LOGGER_TRACE(logger, msg, ...)
#define QUICR_TRACE(msg, ...)
#endif
#if QUICR_ACTIVE_LOG_LEVEL <= QUICR_LOG_DEBUG
#define QUICR_LOGGER_DEBUG(logger, msg, ...)                                                                           \
    QUICR_LOGGER_LOG(logger, quicr::Logger::Level::Debug, msg __VA_OPT__(, ) __VA_ARGS__)
#define QUICR_DEBUG(msg, ...) QUICR_LOG(quicr::Logger::Level::Debug, msg __VA_OPT__(, ) __VA_ARGS__)
#else
#define QUICR_LOGGER_DEBUG(logger, msg, ...)
#define QUICR_DEBUG(msg, ...)
#endif
#if QUICR_ACTIVE_LOG_LEVEL <= QUICR_LOG_INFO
#define QUICR_LOGGER_INFO(logger, msg, ...)                                                                            \
    QUICR_LOGGER_LOG(logger, quicr::Logger::Level::Info, msg __VA_OPT__(, ) __VA_ARGS__)
#define QUICR_INFO(msg, ...) QUICR_LOG(quicr::Logger::Level::Info, msg __VA_OPT__(, ) __VA_ARGS__)
#else
#define QUICR_LOGGER_INFO(logger, msg, ...)
#define QUICR_INFO(msg, ...)
#endif
#if QUICR_ACTIVE_LOG_LEVEL <= QUICR_LOG_WARN
#define QUICR_LOGGER_WARN(logger, msg, ...)                                                                            \
    QUICR_LOGGER_LOG(logger, quicr::Logger::Level::Warn, msg __VA_OPT__(, ) __VA_ARGS__)
#define QUICR_WARN(msg, ...) QUICR_LOG(quicr::Logger::Level::Warn, msg __VA_OPT__(, ) __VA_ARGS__)
#else
#define QUICR_LOGGER_WARN(logger, msg, ...)
#define QUICR_WARN(msg, ...)
#endif
#if QUICR_ACTIVE_LOG_LEVEL <= QUICR_LOG_ERROR
#define QUICR_LOGGER_ERROR(logger, msg, ...)                                                                           \
    QUICR_LOGGER_LOG(logger, quicr::Logger::Level::Error, msg __VA_OPT__(, ) __VA_ARGS__)
#define QUICR_ERROR(msg, ...) QUICR_LOG(quicr::Logger::Level::Error, msg __VA_OPT__(, ) __VA_ARGS__)
#else
#define QUICR_LOGGER_ERROR(logger, msg, ...)
#define QUICR_ERROR(msg, ...)
#endif
#if QUICR_ACTIVE_LOG_LEVEL <= QUICR_LOG_CRITICAL
#define QUICR_LOGGER_CRITICAL(logger, msg, ...)                                                                        \
    QUICR_LOGGER_LOG(logger, quicr::Logger::Level::Critical, msg __VA_OPT__(, ) __VA_ARGS__)
#define QUICR_CRITICAL(msg, ...) QUICR_LOG(quicr::Logger::Level::Critical, msg __VA_OPT__(, ) __VA_ARGS__)
#else
#define QUICR_LOGGER_CRITICAL(logger, msg, ...)
#define QUICR_CRITICAL(msg, ...)
#endif

}
