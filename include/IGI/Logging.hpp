#pragma once

#include <string_view>

namespace igi {
enum class LogLevel { Trace, Debug, Info, Warning, Error, Critical };
using LogSink = void (*)(LogLevel, std::string_view);
void setLogSink(LogSink sink) noexcept;
void log(LogLevel level, std::string_view message) noexcept;
}
