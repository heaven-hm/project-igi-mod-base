#include "igi_sdk/logging/logging.hpp"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <functional>
#include <mutex>
#include <thread>

namespace {
std::mutex logMutex;
igi::LogSink logSink = nullptr;
std::FILE* logFile = nullptr;
bool logFileInitialized = false;
std::atomic<igi::LogLevel> minimumLevel{igi::LogLevel::Trace};

const char* levelName(igi::LogLevel level) noexcept {
    switch (level) {
    case igi::LogLevel::Trace: return "TRACE";
    case igi::LogLevel::Debug: return "DEBUG";
    case igi::LogLevel::Info: return "INFO";
    case igi::LogLevel::Warning: return "WARN";
    case igi::LogLevel::Error: return "ERROR";
    case igi::LogLevel::Critical: return "CRIT";
    }
    return "UNKNOWN";
}

// One line shape everywhere: stable SDK prefix, level, thread, wall clock.
void writeLine(std::FILE* stream, igi::LogLevel level, std::string_view message) noexcept {
    const auto now = std::chrono::system_clock::now();
    const auto seconds = std::chrono::system_clock::to_time_t(now);
    const auto millis = static_cast<long>(
        std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000);
    std::tm local{};
#if defined(_WIN32)
    localtime_s(&local, &seconds);
#else
    localtime_r(&seconds, &local);
#endif
    const auto threadHash = std::hash<std::thread::id>{}(std::this_thread::get_id());
    std::fprintf(stream, "[IGI SDK][%s][t%04lu][%04d-%02d-%02d %02d:%02d:%02d.%03ld] %.*s\n",
                 levelName(level), static_cast<unsigned long>(threadHash % 10000u),
                 local.tm_year + 1900, local.tm_mon + 1, local.tm_mday,
                 local.tm_hour, local.tm_min, local.tm_sec, millis,
                 static_cast<int>(message.size()), message.data());
}
}

namespace igi {
void setLogSink(LogSink sink) noexcept {
    std::lock_guard lock(logMutex);
    logSink = sink;
}

void setLogLevel(LogLevel minimum) noexcept {
    minimumLevel.store(minimum, std::memory_order_relaxed);
}

LogLevel logLevel() noexcept {
    return minimumLevel.load(std::memory_order_relaxed);
}

void log(LogLevel level, std::string_view message) noexcept {
    if (static_cast<int>(level) < static_cast<int>(minimumLevel.load(std::memory_order_relaxed))) return;

    std::lock_guard lock(logMutex);

    if (!logFileInitialized) {
        logFileInitialized = true;
        logFile = std::fopen("project-igi-sdk.log", "ab");
    }

    if (logSink != nullptr) {
        logSink(level, message);
    }
#ifndef NDEBUG
    if (logSink == nullptr) {
        writeLine(stderr, level, message);
    }
#endif

    if (logFile != nullptr) {
        writeLine(logFile, level, message);
        std::fflush(logFile);
    }
}
}
