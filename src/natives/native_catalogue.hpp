#pragma once

#include <cstdint>
#include <iosfwd>
#include <string>
#include <string_view>
#include <vector>

namespace igi {
enum class NativeDomain { AI, Soldiers, Vehicles, Camera, Graphs, Memory, Other };

NativeDomain nativeDomainForName(std::string_view name) noexcept;

enum class NativeVerification { Catalogued, Verified, Exposed };

struct NativeRecord {
    std::uint32_t address = 0;
    std::string name;
    std::string provenance;
    std::string kind;
    std::string signature;
    NativeVerification verification = NativeVerification::Catalogued;
    // A generated wrapper does not imply an in-game invocation was observed.
    bool runtimeVerified = false;
};

class NativeCatalogue final {
  public:
    bool load(std::istream &input);
    std::size_t size() const noexcept {
        return records_.size();
    }
    const std::vector<NativeRecord> &records() const noexcept {
        return records_;
    }
    std::size_t count(NativeDomain domain) const noexcept;
    const NativeRecord *findByAddress(std::uint32_t address) const noexcept;
    // Returns null for absent or ambiguous names; callers may resolve by address.
    const NativeRecord *findByName(std::string_view name) const noexcept;
    // Promotion is explicit: imported research remains non-callable until a
    // target-build review promotes the exact record.
    bool markVerified(std::string_view name,
                      NativeVerification state = NativeVerification::Verified) noexcept;
    bool add(NativeRecord record);

  private:
    std::vector<NativeRecord> records_;
};
} // namespace igi
