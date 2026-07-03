#pragma once

#include <Arduino.h>

namespace rvf {

enum class LogLevel { Info, Warning, Error };

void logf(LogLevel level, const char* tag, const char* format, ...);
void logLine(LogLevel level, const char* tag, const char* message);

}  // namespace rvf

#define LOGI(tag, fmt, ...) ::rvf::logf(::rvf::LogLevel::Info, tag, fmt, ##__VA_ARGS__)
#define LOGW(tag, fmt, ...) ::rvf::logf(::rvf::LogLevel::Warning, tag, fmt, ##__VA_ARGS__)
#define LOGE(tag, fmt, ...) ::rvf::logf(::rvf::LogLevel::Error, tag, fmt, ##__VA_ARGS__)
#define LOGLINE_I(tag, msg) ::rvf::logLine(::rvf::LogLevel::Info, tag, msg)
#define LOGLINE_W(tag, msg) ::rvf::logLine(::rvf::LogLevel::Warning, tag, msg)
#define LOGLINE_E(tag, msg) ::rvf::logLine(::rvf::LogLevel::Error, tag, msg)
