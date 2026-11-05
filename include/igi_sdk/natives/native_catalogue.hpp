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
};

class NativeCatalogue final {
public:
    bool load(std::istream& input);
    std::size_t size() const noexcept { return records_.size(); }
    const std::vector<NativeRecord>& records() const noexcept { return records_; }
    std::size_t count(NativeDomain domain) const noexcept;
    const NativeRecord* findByAddress(std::uint32_t address) const noexcept;
    const NativeRecord* findByName(std::string_view name) const noexcept;

private:
    std::vector<NativeRecord> records_;
};
}
