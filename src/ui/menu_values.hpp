#pragma once

#include "config/menu_config.hpp"
#include "ui/menu_commands.hpp"

#include <cstddef>
#include <functional>
#include <optional>
#include <string_view>
#include <vector>

namespace igi {
// Owns a copy of configured choices; call from the game thread.
class MenuValues final {
  public:
    explicit MenuValues(const MenuConfig &config);
    std::size_t itemCount() const noexcept {
        return items_.size();
    }
    std::size_t choiceCount(std::size_t item) const noexcept;
    std::optional<std::string_view> choice(std::size_t item, std::size_t index) const noexcept;
    std::optional<std::size_t> selected(std::size_t item) const noexcept;
    bool select(std::size_t item, std::size_t index) noexcept;
    std::optional<bool> toggle(std::size_t item) const noexcept;
    bool setToggle(std::size_t item, bool value) noexcept;
    // Feed IGI's currently filling ListBox through a supplied game-thread adapter.
    bool fill(std::size_t item, const std::function<void(std::string_view, int)> &addItem) const;
    // Re-read choices from the live config so a list rebuilt from the game task
    // table (live NPC/car targets) is what the retail ListBox collects. Returns
    // true when any list content changed and the screen must re-collect.
    bool resync(const MenuConfig &config);

    void snapshot() noexcept;
    void revert() noexcept;
    void commit() noexcept;
    bool selectedChanged(std::size_t item) const noexcept;
    bool toggleChanged(std::size_t item) const noexcept;

  private:
    // Number of choices a control exposes to the retail ListBox. A numeric input
    // exposes its sample values because QVM menus have no text widget.
    std::size_t choiceCountFor(const ModMenuItemConfig &item) const;
    std::string choiceLabelFor(const ModMenuItemConfig &item, std::size_t index) const;

    std::vector<ModMenuItemConfig> items_;
    std::vector<std::vector<std::string>> inputLabels_;
    std::vector<std::size_t> selected_;
    std::vector<bool> toggles_;
    std::vector<std::size_t> committedSelected_;
    std::vector<bool> committedToggles_;
};
} // namespace igi
