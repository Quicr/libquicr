// SPDX-FileCopyrightText: Copyright (c) 2026 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <quicr/log.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

class SpdlogLogger : public quicr::Logger
{
  public:
    SpdlogLogger(const std::string& name)
      : logger_(spdlog::get(name) ? spdlog::get(name) : spdlog::stderr_color_mt(name))
    {
    }

    virtual ~SpdlogLogger() = default;

    void SetLevel(Level max_level) override { logger_->set_level(ConvertLevelType(max_level)); }

    bool ShouldLog(Level level) const noexcept override { return logger_->should_log(ConvertLevelType(level)); }

    void Log(quicr::Logger::Level level,
             std::string_view msg,
             std::source_location location = std::source_location::current()) override
    try {
        logger_->log(spdlog::source_loc(location.file_name(), location.line(), location.function_name()),
                     ConvertLevelType(level),
                     msg);
    } catch (const std::exception& e) {
        logger_->log(spdlog::source_loc(location.file_name(), location.line(), location.function_name()),
                     spdlog::level::err,
                     "log failed to format (error={})",
                     e.what());
    }

    void Log(quicr::Logger::Level level, std::string_view msg, spdlog::source_loc location)
    {
        logger_->log(location, ConvertLevelType(level), msg);
    }

    operator const std::shared_ptr<spdlog::logger>&() const noexcept { return logger_; }

  protected:
    spdlog::level::level_enum ConvertLevelType(Logger::Level level) const noexcept
    {
        switch (level) {
            case Logger::Level::Trace:
                return spdlog::level::trace;
            case Logger::Level::Debug:
                return spdlog::level::debug;
            case Logger::Level::Info:
                return spdlog::level::info;
            case Logger::Level::Warn:
                return spdlog::level::warn;
            case Logger::Level::Error:
                return spdlog::level::err;
            case Logger::Level::Critical:
                return spdlog::level::critical;
            case Logger::Level::Off:
                return spdlog::level::off;
        }
    }

  protected:
    std::shared_ptr<spdlog::logger> logger_;
};
