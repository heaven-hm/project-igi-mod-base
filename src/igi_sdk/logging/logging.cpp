#include "igi_sdk/logging/logging.hpp"

#include <cstdio>
#include <mutex>

namespace {
std::mutex logMutex;
igi::LogSink logSink = nullptr;
std::FILE* logFile = nullptr;
bool logFileInitialized = false;
}

namespace igi {
void setLogSink(LogSink sink) noexcept {
    std::lock_guard lock(logMutex);
    logSink = sink;
}

void log(LogLevel level, std::string_view message) noexcept {
    std::lock_guard lock(logMutex);

    if (!logFileInitialized) {
        logFileInitialized = true;
        logFile = std::fopen("igi_sdk.log", "ab");
    }

    const auto formattedLevel = static_cast<int>(level);
    if (logSink != nullptr) {
        logSink(level, message);
#ifndef NDEBUG
    } else {
        std::fprintf(stderr, "[IGI SDK][%d] %.*s\n", formattedLevel,
                     static_cast<int>(message.size()), message.data());
#endif
    }

    if (logFile != nullptr) {
        std::fprintf(logFile, "[IGI SDK][%d] %.*s\n", formattedLevel,
                     static_cast<int>(message.size()), message.data());
        std::fflush(logFile);
    }
}
}
