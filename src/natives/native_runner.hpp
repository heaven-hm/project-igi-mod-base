#pragma once

#include "natives/native_catalogue.hpp"
#include "runtime/scheduler.hpp"

#include <functional>
#include <string_view>

namespace igi {
class NativeRunner final {
  public:
    using ResolvedCallback = std::function<void(const NativeRecord &)>;

    NativeRunner(const NativeCatalogue &catalogue, Scheduler &scheduler) noexcept
        : catalogue_(catalogue), scheduler_(scheduler) {}

    TaskId queueResolution(std::string_view name, ResolvedCallback callback);

  private:
    const NativeCatalogue &catalogue_;
    Scheduler &scheduler_;
};
} // namespace igi
