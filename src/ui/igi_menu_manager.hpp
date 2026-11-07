#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace igi {
struct MenuConfig;
enum class IgiMenuSet { Main, InGame };

enum class IgiWidgetKind {
    MenuFrame,
    MenuText,
    MenuTextConditional,
    SlideBar,
    ListBox,
    ToggleBox,
    InputBox,
    AsciiBox,
    ScrollListBox,
    PictureBox,
    TextBox,
    TypeWriterBox,
    ControlsMenu,
    DialogWindow
};

struct IgiMenuWidget {
    IgiWidgetKind kind;
    std::int32_t id = -1;
    std::string key;
    std::string label;
    std::string resource;
    std::string readScript;
    std::string writeScript;
    std::string clickScript;
    std::string clickSound;
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::int32_t width = 0;
    std::int32_t height = 0;
    bool selectable = false;
};

struct IgiMenuScreen {
    std::int32_t id = 0;
    std::string title;
    std::string background;
    std::string escapeScript;
    std::vector<IgiMenuWidget> widgets;
};

class IgiMenuManager final {
  public:
    static IgiMenuManager create(IgiMenuSet set);
    static IgiMenuManager create(IgiMenuSet set, const MenuConfig &config);

    IgiMenuSet menuSet() const noexcept {
        return menuSet_;
    }
    const std::vector<IgiMenuScreen> &screens() const noexcept {
        return screens_;
    }
    const IgiMenuScreen *currentScreen() const noexcept;
    bool pushScreen(std::int32_t id);
    bool requestScreen(std::int32_t id, bool commit);
    bool popScreen(bool commit);
    bool activatePopup(std::int32_t id);
    bool leaveMenus(std::int32_t id, std::string_view reason);
    void reset() noexcept;

  private:
    explicit IgiMenuManager(IgiMenuSet set) : menuSet_(set) {}
    const IgiMenuScreen *findScreen(std::int32_t id) const noexcept;
    IgiMenuScreen *findScreen(std::int32_t id) noexcept;

    IgiMenuSet menuSet_;
    std::vector<IgiMenuScreen> screens_;
    std::vector<std::int32_t> screenStack_;
    std::int32_t currentScreenId_ = 0;
    std::int32_t pendingScreenId_ = 0;
};
} // namespace igi
