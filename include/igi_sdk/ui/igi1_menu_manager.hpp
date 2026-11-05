#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace igi {
enum class Igi1MenuSet { Main, InGame };

enum class Igi1WidgetKind {
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

struct Igi1MenuWidget {
    Igi1WidgetKind kind;
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

struct Igi1MenuScreen {
    std::int32_t id = 0;
    std::string title;
    std::string background;
    std::string escapeScript;
    std::vector<Igi1MenuWidget> widgets;
};

class Igi1MenuManager final {
public:
    static Igi1MenuManager create(Igi1MenuSet set);

    Igi1MenuSet menuSet() const noexcept { return menuSet_; }
    const std::vector<Igi1MenuScreen>& screens() const noexcept { return screens_; }
    const Igi1MenuScreen* currentScreen() const noexcept;
    bool pushScreen(std::int32_t id);
    bool requestScreen(std::int32_t id, bool commit);
    bool popScreen(bool commit);
    bool activatePopup(std::int32_t id);
    bool leaveMenus(std::int32_t id, std::string_view reason);
    void reset() noexcept;

private:
    explicit Igi1MenuManager(Igi1MenuSet set) : menuSet_(set) {}
    const Igi1MenuScreen* findScreen(std::int32_t id) const noexcept;
    Igi1MenuScreen* findScreen(std::int32_t id) noexcept;

    Igi1MenuSet menuSet_;
    std::vector<Igi1MenuScreen> screens_;
    std::vector<std::int32_t> screenStack_;
    std::int32_t currentScreenId_ = 0;
    std::int32_t pendingScreenId_ = 0;
};
}
