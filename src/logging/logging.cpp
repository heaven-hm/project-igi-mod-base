#include "logging/logging.hpp"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace {
std::mutex logMutex;
igi::LogSink logSink = nullptr;
std::FILE *logFile = nullptr;
bool logPathReported = false;
std::string logFilename = "project-igi-mod.log";
std::string logFilenameFallback;
std::chrono::steady_clock::time_point logOpenAttemptTime{};
std::atomic<igi::LogLevel> minimumLevel{igi::LogLevel::Trace};

const char *levelName(igi::LogLevel level) noexcept {
    switch (level) {
    case igi::LogLevel::Trace:
        return "TRACE";
    case igi::LogLevel::Debug:
        return "DEBUG";
    case igi::LogLevel::Info:
        return "INFO";
    case igi::LogLevel::Warning:
        return "WARN";
    case igi::LogLevel::Error:
        return "ERROR";
    case igi::LogLevel::Critical:
        return "CRIT";
    }
    return "UNKNOWN";
}

// One line shape everywhere: stable SDK prefix, level, thread, wall clock.
void writeLine(std::FILE *stream, igi::LogLevel level, std::string_view message) noexcept {
    const auto now = std::chrono::system_clock::now();
    const auto seconds = std::chrono::system_clock::to_time_t(now);
    const auto millis = static_cast<long>(
        std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() %
        1000);
    std::tm local{};
#if defined(_WIN32)
    localtime_s(&local, &seconds);
#else
    localtime_r(&seconds, &local);
#endif
    const auto threadHash = std::hash<std::thread::id>{}(std::this_thread::get_id());
    std::fprintf(stream, "[IGI MOD][%s][t%04lu][%04d-%02d-%02d %02d:%02d:%02d.%03ld] %.*s\n",
                 levelName(level), static_cast<unsigned long>(threadHash % 10000u),
                 local.tm_year + 1900, local.tm_mon + 1, local.tm_mday, local.tm_hour, local.tm_min,
                 local.tm_sec, millis, static_cast<int>(message.size()), message.data());
}
} // namespace

namespace igi {
void setLogFile(std::string_view filename) {
    if (filename.empty() || filename == "." || filename == ".." ||
        filename.find_first_of("/\\:") != std::string_view::npos ||
        filename.find('\0') != std::string_view::npos)
        return;
    std::lock_guard lock(logMutex);
    if (logFilename == filename && logFile != nullptr)
        return;
    if (logFile)
        std::fclose(logFile);
    logFile = nullptr;
    logPathReported = false;
    logFilenameFallback.clear();
    // A new name must be attempted immediately, not after the retry backoff.
    logOpenAttemptTime = std::chrono::steady_clock::time_point::min();
    logFilename.assign(filename);
}

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
    if (static_cast<int>(level) < static_cast<int>(minimumLevel.load(std::memory_order_relaxed)))
        return;

    std::lock_guard lock(logMutex);

    // Open lazily and RETRY after a failure. The old one-shot latch meant a
    // single failed open (another igi.exe still holding the file, or a different
    // working directory after a game restart) silenced the whole session, which
    // is exactly why no log existed for the last report.
    if (logFile == nullptr) {
        const auto now = std::chrono::steady_clock::now();
        if (logOpenAttemptTime == decltype(logOpenAttemptTime)::min() ||
            now - logOpenAttemptTime >= std::chrono::seconds(5)) {
            logOpenAttemptTime = now;
            logFile = std::fopen(logFilename.c_str(), "ab");
            if (logFile != nullptr)
                logFilenameFallback.clear();
            if (logFile == nullptr) {
                // Fall back to a per-process file next to the module so a locked
                // or non-writable working directory still leaves a trail.
                const std::string fallback =
                    logFilename + "." +
                    std::to_string(static_cast<unsigned long long>(
                        std::hash<std::thread::id>{}(std::this_thread::get_id()))) +
                    ".log";
                logFile = std::fopen(fallback.c_str(), "ab");
                if (logFile != nullptr)
                    logFilenameFallback = fallback;
            }
        }
    }

    // Report the resolved path once so a "no log file" report can be told apart
    // from "the log lives somewhere else".
    if (logFile != nullptr && !logPathReported) {
        logPathReported = true;
        const std::string path = logFilenameFallback.empty() ? logFilename : logFilenameFallback;
        char absolute[4096]{};
#if defined(_WIN32)
        const auto written = ::GetFullPathNameA(path.c_str(), sizeof(absolute), absolute, nullptr);
        if (written > 0 && written < sizeof(absolute))
            writeLine(logFile, igi::LogLevel::Info, "log file: " + std::string(absolute));
        else
#endif
            writeLine(logFile, igi::LogLevel::Info, "log file: " + path);
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

void closeLogFile() noexcept {
    std::lock_guard lock(logMutex);
    if (logFile != nullptr) {
        std::fflush(logFile);
        std::fclose(logFile);
        logFile = nullptr;
    }
    logPathReported = false;
}
} // namespace igi
