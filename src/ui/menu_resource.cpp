#include "ui/menu_resource.hpp"

#include <limits>
#include <string_view>
#include <utility>

namespace igi {
bool MenuResource::registerData(std::string name, std::vector<std::uint8_t> data,
                                MenuResourceApi api) {
    const auto expandedName = name;
    return registerData(std::move(name), expandedName, std::move(data), api);
}

bool MenuResource::registerData(std::string name, std::string expandedName,
                                std::vector<std::uint8_t> data, MenuResourceApi api) {
    if (active() || name.empty() || name.size() >= 128 || name.find('\0') != std::string::npos ||
        expandedName.empty() || expandedName.size() >= 128 ||
        expandedName.find('\0') != std::string::npos || data.empty() ||
        data.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()) || !api.isLoaded ||
        !api.registerResource || !api.freeResource)
        return false;
    // A resource still registered under one of our OWN LOCAL:mod_base names after
    // a level restart points at QVM memory the previous level already invalidated
    // (or at our own freed image). Running it produces the empty frame with no
    // options, so reclaim that name and register the fresh bytes instead. Any
    // other namespace belongs to someone else and is never touched.
    constexpr std::string_view kOwnedPrefix = "LOCAL:mod_base/";
    const bool ownedName = std::string_view(name).starts_with(kOwnedPrefix) ||
                           std::string_view(expandedName).starts_with(kOwnedPrefix);
    void *existing = nullptr;
    if (api.isLoaded(name.c_str(), &existing) || api.isLoaded(expandedName.c_str(), &existing)) {
        if (!ownedName || !existing)
            return false;
        api.freeResource(existing);
        existing = nullptr;
    }
    name_ = std::move(name);
    data_ = std::move(data);
    api_ = api;
    resource_ =
        api_.registerResource(expandedName.c_str(), static_cast<int>(data_.size()), data_.data());
    if (!resource_) {
        name_.clear();
        data_.clear();
        api_ = {};
        return false;
    }
    // A successful registration is useful only if IGI resolves the lookup
    // path back to the exact resource we own. Roll back on an alias mismatch.
    existing = nullptr;
    if (!api_.isLoaded(name_.c_str(), &existing) || existing != resource_) {
        api_.freeResource(resource_);
        resource_ = nullptr;
        name_.clear();
        data_.clear();
        api_ = {};
        return false;
    }
    return true;
}

bool MenuResource::release() noexcept {
    if (!resource_)
        return true;
    void *existing = nullptr;
    if (!api_.isLoaded(name_.c_str(), &existing) || existing != resource_)
        return false;
    api_.freeResource(resource_);
    resource_ = nullptr;
    name_.clear();
    data_.clear();
    api_ = {};
    return true;
}

void MenuResource::abandon() noexcept {
    resource_ = nullptr;
    name_.clear();
    data_.clear();
    api_ = {};
}
} // namespace igi
