
#include "testLogger.h"
#include <chrono>
#include <iomanip>
#include <iostream>

void testLogger::log(qtransport::LogLevel level, const std::string &string) {
  const char *lvl;
  switch (level) {
  case qtransport::LogLevel::fatal:
    lvl = "FATAL";
    break;
  case qtransport::LogLevel::error:
    lvl = "ERROR";
    break;
  case qtransport::LogLevel::warn:
    lvl = "WARN";
    break;
  default:
    lvl = "INFO";
    break;
  }

  auto now = std::chrono::system_clock::now();
  const auto nowAsTimeT = std::chrono::system_clock::to_time_t(now);
  const auto nowUs = std::chrono::duration_cast<std::chrono::microseconds>(
                         now.time_since_epoch()) %
                     1000000;

  std::lock_guard lock(mutex);
  std::cout << std::put_time(std::localtime(&nowAsTimeT), "%m-%d-%Y %H:%M:%S")
            << "." << std::setfill('0') << std::setw(6) << nowUs.count()
            << std::setfill(' ') << " " << std::setw(6) << std::right << lvl
            << std::setw(0) << " | " << string << std::endl;
}
