#include "igi_sdk/natives/native_catalogue.hpp"

#include <charconv>
#include <istream>
#include <string>
#include <unordered_set>

namespace {
bool parseAddress(std::string_view source, std::uint32_t& address) {
    unsigned parsed = 0;
    const auto [end, error] = std::from_chars(source.data(), source.data() + source.size(), parsed, 16);
    if (error != std::errc{} || end != source.data() + source.size()) return false;
    address = parsed;
    return true;
}
}

namespace igi {
bool NativeCatalogue::load(std::istream& input) {
    std::vector<NativeRecord> parsed;
    std::unordered_set<std::uint32_t> addresses;
    std::string line;
    while (std::getline(input, line)) {
        if (line.empty() || line.front() == '#') continue;
        std::string fields[5];
        std::size_t field = 0;
        std::size_t start = 0;
        while (field < 4) {
            const std::size_t delimiter = line.find('\t', start);
            if (delimiter == std::string::npos) break;
            fields[field++] = line.substr(start, delimiter - start);
            start = delimiter + 1;
        }
        if (field != 4) return false;
        fields[4] = line.substr(start);
        NativeRecord record;
        if (!parseAddress(fields[0], record.address)) return false;
        record.name = std::move(fields[1]);
        if (record.name.empty() || !addresses.insert(record.address).second)
            return false;
        record.provenance = std::move(fields[2]);
        record.kind = std::move(fields[3]);
        record.signature = std::move(fields[4]);
        parsed.push_back(std::move(record));
    }
    records_ = std::move(parsed);
    return !records_.empty();
}

const NativeRecord* NativeCatalogue::findByAddress(std::uint32_t address) const noexcept {
    for (const NativeRecord& record : records_) if (record.address == address) return &record;
    return nullptr;
}

const NativeRecord* NativeCatalogue::findByName(std::string_view name) const noexcept {
    for (const NativeRecord& record : records_) if (record.name == name) return &record;
    return nullptr;
}
}
