#include "quicr/log.h"

#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include <memory>
#include <string_view>

namespace quicr {

    class Logger::Impl
    {
      public:
        Impl(const std::string& name)
          : logger_(spdlog::get(name) ? spdlog::get(name) : spdlog::stderr_color_mt(name))
        {
        }

        void SetLevel(Logger::Level max_level) { logger_->set_level(ConvertLevelType(max_level)); }

        void Log(Logger::Level level, std::string_view msg, std::source_location location)
        {
            logger_->log(spdlog::source_loc(location.file_name(), location.line(), location.function_name()),
                         ConvertLevelType(level),
                         msg);
        }

      private:
        spdlog::level::level_enum ConvertLevelType(Logger::Level level)
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
            }
        }

      private:
        std::shared_ptr<spdlog::logger> logger_;
    };

    Logger::Logger(std::string_view name)
      : impl_(std::make_unique<Impl>(std::string(name)))
    {
    }

    std::shared_ptr<Logger> Logger::Create(std::string_view name)
    {
        return std::shared_ptr<Logger>(new Logger(name));
    }

    std::shared_ptr<Logger> Logger::GetDefault()
    {
        static auto default_logger = Logger::Create("");
        return default_logger;
    }

    void Logger::SetLevel(Level max_level)
    {
        impl_->SetLevel(max_level);
    }

    void Logger::Log(Level level, std::string_view msg, std::source_location location)
    {
        impl_->Log(level, msg, location);
    }

}
