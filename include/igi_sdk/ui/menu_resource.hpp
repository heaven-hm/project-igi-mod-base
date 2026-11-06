#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace igi {
class NativeCaller;
// Functions match IGI 1 Resource_IsLoaded/Register/Free; call on game thread.
struct MenuResourceApi {
    int (*isLoaded)(const char*, void**) = nullptr;
    void* (*registerResource)(const char*, int, void*) = nullptr;
    void (*freeResource)(void*) = nullptr;
};

// Returns empty unless the supported x86 runtime is on its game thread.
MenuResourceApi igi1MenuResourceApi(NativeCaller& caller) noexcept;

class MenuResource final {
public:
    MenuResource() = default;
    MenuResource(const MenuResource&) = delete;
    MenuResource& operator=(const MenuResource&) = delete;
    bool registerData(std::string name, std::vector<std::uint8_t> data, MenuResourceApi api);
    // IGI resolves lookup aliases but stores registration names literally.
    // The caller must provide the matching, already-expanded device path.
    bool registerData(std::string lookupName, std::string expandedName,
                      std::vector<std::uint8_t> data, MenuResourceApi api);
    bool release() noexcept;
    bool active() const noexcept { return resource_ != nullptr; }
private:
    std::string name_;
    std::vector<std::uint8_t> data_;
    MenuResourceApi api_{};
    void* resource_ = nullptr;
};
}
