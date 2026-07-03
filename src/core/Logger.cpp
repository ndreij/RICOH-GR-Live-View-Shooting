#include "Logger.h"

#include <stdarg.h>
#include <stdio.h>

namespace rvf {
namespace {
const char* levelText(LogLevel level) {
    switch (level) {
        case LogLevel::Info: return "I";
        case LogLevel::Warning: return "W";
        case LogLevel::Error: return "E";
    }
    return "?";
}

void printPrefix(LogLevel level, const char* tag) {
    Serial.printf("[%s][%s] ", levelText(level), tag != nullptr ? tag : "APP");
}
}  // namespace

void logf(LogLevel level, const char* tag, const char* format, ...) {
    char buffer[256];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format != nullptr ? format : "", args);
    va_end(args);

    printPrefix(level, tag);
    Serial.print(buffer);
    Serial.println();
}

void logLine(LogLevel level, const char* tag, const char* message) {
    printPrefix(level, tag);
    Serial.println(message != nullptr ? message : "");
}

}  // namespace rvf
