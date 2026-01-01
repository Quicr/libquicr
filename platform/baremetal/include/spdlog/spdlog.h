/*
 * SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Bare-metal stub for spdlog
 * Provides minimal no-op logging for builds without spdlog
 */

#ifndef QUICR_BAREMETAL_SPDLOG_H
#define QUICR_BAREMETAL_SPDLOG_H

#include <memory>
#include <string>
#include <cstdio>

namespace spdlog {

namespace level {
    enum level_enum {
        trace = 0,
        debug = 1,
        info = 2,
        warn = 3,
        err = 4,
        critical = 5,
        off = 6
    };
}

class logger {
public:
    logger(const std::string& name) : name_(name) {}

    template<typename... Args>
    void trace(const char* fmt, Args&&... args) { (void)fmt; ((void)args, ...); }

    template<typename... Args>
    void debug(const char* fmt, Args&&... args) { (void)fmt; ((void)args, ...); }

    template<typename... Args>
    void info(const char* fmt, Args&&... args) { (void)fmt; ((void)args, ...); }

    template<typename... Args>
    void warn(const char* fmt, Args&&... args) { (void)fmt; ((void)args, ...); }

    template<typename... Args>
    void error(const char* fmt, Args&&... args) { (void)fmt; ((void)args, ...); }

    template<typename... Args>
    void critical(const char* fmt, Args&&... args) { (void)fmt; ((void)args, ...); }

    void set_level(level::level_enum lvl) { level_ = lvl; }
    level::level_enum get_level() const { return level_; }
    const std::string& name() const { return name_; }

private:
    std::string name_;
    level::level_enum level_ = level::info;
};

inline std::shared_ptr<logger> default_logger() {
    static auto logger_ = std::make_shared<logger>("default");
    return logger_;
}

inline std::shared_ptr<logger> get(const std::string& name) {
    return std::make_shared<logger>(name);
}

inline void set_level(level::level_enum lvl) {
    default_logger()->set_level(lvl);
}

inline void set_default_logger(std::shared_ptr<logger> logger) {
    (void)logger;
}

/* Sink factory functions - return shared_ptr to logger */
inline std::shared_ptr<logger> stderr_color_mt(const std::string& name) {
    return std::make_shared<logger>(name);
}

inline std::shared_ptr<logger> stdout_color_mt(const std::string& name) {
    return std::make_shared<logger>(name);
}

/* Drop a logger by name - no-op for stub */
inline void drop(const std::string& name) {
    (void)name;
}

/* Drop all loggers - no-op for stub */
inline void drop_all() {}

template<typename... Args>
inline void trace(const char* fmt, Args&&... args) { (void)fmt; ((void)args, ...); }

template<typename... Args>
inline void debug(const char* fmt, Args&&... args) { (void)fmt; ((void)args, ...); }

template<typename... Args>
inline void info(const char* fmt, Args&&... args) { (void)fmt; ((void)args, ...); }

template<typename... Args>
inline void warn(const char* fmt, Args&&... args) { (void)fmt; ((void)args, ...); }

template<typename... Args>
inline void error(const char* fmt, Args&&... args) { (void)fmt; ((void)args, ...); }

template<typename... Args>
inline void critical(const char* fmt, Args&&... args) { (void)fmt; ((void)args, ...); }

} // namespace spdlog

/* SPDLOG macros - just ignore */
#define SPDLOG_TRACE(...) do {} while(0)
#define SPDLOG_DEBUG(...) do {} while(0)
#define SPDLOG_INFO(...) do {} while(0)
#define SPDLOG_WARN(...) do {} while(0)
#define SPDLOG_ERROR(...) do {} while(0)
#define SPDLOG_CRITICAL(...) do {} while(0)

#define SPDLOG_LOGGER_TRACE(logger, ...) do {} while(0)
#define SPDLOG_LOGGER_DEBUG(logger, ...) do {} while(0)
#define SPDLOG_LOGGER_INFO(logger, ...) do {} while(0)
#define SPDLOG_LOGGER_WARN(logger, ...) do {} while(0)
#define SPDLOG_LOGGER_ERROR(logger, ...) do {} while(0)
#define SPDLOG_LOGGER_CRITICAL(logger, ...) do {} while(0)

#endif /* QUICR_BAREMETAL_SPDLOG_H */
