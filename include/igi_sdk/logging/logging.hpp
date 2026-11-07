#pragma once

#include <string_view>

namespace igi {
enum class LogLevel { Trace, Debug, Info, Warning, Error, Critical };
using LogSink = void (*)(LogLevel, std::string_view);

void setLogSink(LogSink sink) noexcept;
void setLogFile(std::string_view filename);

// Messages below the minimum are dropped before formatting. The default is
// Trace so a host that never configures logging keeps every message.
void setLogLevel(LogLevel minimum) noexcept;
LogLevel logLevel() noexcept;

void log(LogLevel level, std::string_view message) noexcept;
void closeLogFile() noexcept;
}
