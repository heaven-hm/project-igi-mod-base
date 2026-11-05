#include "IGI/Logging.hpp"

#include <cstdio>
#include <mutex>

namespace {
std::mutex logMutex;
igi::LogSink logSink = nullptr;
}

namespace igi {
void setLogSink(LogSink sink) noexcept {
    std::lock_guard lock(logMutex);
    logSink = sink;
}

void log(LogLevel level, std::string_view message) noexcept {
    std::lock_guard lock(logMutex);
    if (logSink != nullptr) {
        logSink(level, message);
        return;
    }
    std::fprintf(stderr, "[IGI SDK][%d] %.*s\n", static_cast<int>(level),
                 static_cast<int>(message.size()), message.data());
}
}
