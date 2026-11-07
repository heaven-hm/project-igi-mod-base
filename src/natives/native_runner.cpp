#include "natives/native_runner.hpp"

#include "logging/logging.hpp"

namespace igi {
TaskId NativeRunner::queueResolution(std::string_view name, ResolvedCallback callback) {
    const NativeRecord *record = catalogue_.findByName(name);
    if (record == nullptr || !callback) {
        log(LogLevel::Warning, "native runner could not resolve requested native");
        return 0;
    }

    NativeRecord copy = *record;
    return scheduler_.enqueueOnce(
        [callback = std::move(callback), record = std::move(copy)] { callback(record); });
}
} // namespace igi
