#pragma once

#include "ui/menu_commands.hpp"
#include "ui/menu_values.hpp"

#include <optional>

namespace igi {
class NativeCaller;
// Platform-neutral portion of the IGI script callbacks. The runtime adapter
// must invoke these on the game thread while the game is filling a list.
class MenuScriptBridge final {
  public:
    using SelectionHandler = std::function<void(std::size_t, std::string_view)>;
    using CloseHandler = std::function<void()>;
    using PageHandler = std::function<void(int)>;
    // Runs on the game frame after the Restart button returns; the host detaches
    // the menu and reloads the level through the retail restart path.
    using RestartHandler = std::function<void()>;
    explicit MenuScriptBridge(const MenuConfig &config, SelectionHandler onSelection = {},
                              CloseHandler onClose = {}, bool deferUntilApply = false,
                              PageHandler onPage = {}, RestartHandler onRestart = {})
        : values_(config), commands_(buildMenuCommands(config)),
          onSelection_(std::move(onSelection)), onClose_(std::move(onClose)),
          deferUntilApply_(deferUntilApply), onPage_(std::move(onPage)),
          onRestart_(std::move(onRestart)), screenId_(config.screenId) {}
    // Item index used by the generated script for the flattened command list.
    static constexpr int kCommandList = -1;
    // The retail ListBox is a fixed-size widget, so the flattened command list is
    // shown one page at a time with Prev/Next entries instead of one huge list.
    static constexpr std::size_t kCommandsPerPage = 16;
    std::size_t commandPageCount() const noexcept {
        return commands_.empty() ? 0 : (commands_.size() + kCommandsPerPage - 1) / kCommandsPerPage;
    }
    bool fillList(int item, const std::function<void(std::string_view, int)> &addItem);
    // Aligns the script-visible choices with the live menu config. The retail
    // menu host calls this from the frame tick while its screen is attached.
    bool resync(const MenuConfig &config);
    const std::vector<MenuCommand> &commands() const noexcept {
        return commands_;
    }
    int getList(int item) const noexcept;
    bool setList(int item, int value);
    bool activate(int item);
    int getToggle(int item) const noexcept;
    bool setToggle(int item, int value);
    void close();
    // Retail menu page navigation (IGI_SDK_Page script symbol).
    void page(int delta);
    // Restart requested from the generated button bar.
    void restart();
    void apply();
    // A host completing an already-observed retail close must revert values
    // without feeding the close callback back into a second transition.
    void cancel(bool notifyClose = true);
    void snapshot() noexcept {
        values_.snapshot();
    }

  private:
    // Runs the command entry the retail ListBox last selected.
    bool runSelectedCommand();
    // Asks the attached retail screen to re-collect its list after a page change.
    void refreshScreen();

    MenuValues values_;
    std::vector<MenuCommand> commands_;
    std::optional<std::size_t> selectedCommand_;
    std::size_t commandPage_ = 0;
    SelectionHandler onSelection_;
    CloseHandler onClose_;
    PageHandler onPage_;
    RestartHandler onRestart_;
    bool deferUntilApply_ = false;
    int screenId_ = -1;
};

// IGI 1 x86 ScriptValue layout, as recovered from the game interface.
struct IgiScriptValue {
    int integer;
    int reserved1;
    double real;
    const char *text;
    int reserved2;
};
#if defined(_WIN32) && (defined(__i386__) || defined(_M_IX86))
static_assert(sizeof(IgiScriptValue) == 24);
static_assert(offsetof(IgiScriptValue, real) == 8);
static_assert(offsetof(IgiScriptValue, text) == 16);
#endif

struct MenuScriptApi {
    using GetInt = int (*)(void *, int);
    using AddItem = void (*)(const char *, int);
    GetInt getInt = nullptr;
    AddItem addItem = nullptr;
};

class MenuScriptCallbacks final {
  public:
    MenuScriptCallbacks(MenuScriptBridge &bridge, MenuScriptApi api) : bridge_(bridge), api_(api) {}
    IgiScriptValue *fill(IgiScriptValue *output, void *args);
    IgiScriptValue *get(IgiScriptValue *output, void *args);
    IgiScriptValue *set(IgiScriptValue *output, void *args);
    IgiScriptValue *getToggle(IgiScriptValue *output, void *args);
    IgiScriptValue *setToggle(IgiScriptValue *output, void *args);
    IgiScriptValue *action(IgiScriptValue *output, void *args);
    IgiScriptValue *page(IgiScriptValue *output, void *args);
    IgiScriptValue *close(IgiScriptValue *output, void *args);
    IgiScriptValue *apply(IgiScriptValue *output, void *args);
    IgiScriptValue *cancel(IgiScriptValue *output, void *args);
    IgiScriptValue *restart(IgiScriptValue *output, void *args);

  private:
    MenuScriptBridge &bridge_;
    MenuScriptApi api_;
};

using ScriptCallback = IgiScriptValue *(*)(IgiScriptValue *, void *, void *);
// Minimal recovered IGI 1 symbol fields needed for ownership verification.
struct IgiScriptSymbol {
    int kind;
    int type;
    int dynamic;
    int size;
    ScriptCallback handler;
    const char *name;
    void *data;
};

struct ScriptRegistryApi {
    void (*registerFunction)(const char *, ScriptCallback, void *, int) = nullptr;
    void (*registerIntFunction)(const char *, ScriptCallback, void *, int) = nullptr;
    void (*removeSymbol)(const char *) = nullptr;
    bool (*containsSymbol)(const char *) = nullptr;
    bool (*matchesSymbol)(const char *, ScriptCallback, void *) = nullptr;
    // Reads the live symbol entry. Used to tell our own stale registration (the
    // handler is one of our trampolines but its context object is gone) from a
    // foreign mod's symbol, which must never be removed.
    const IgiScriptSymbol *(*findSymbol)(const char *) = nullptr;
};

// Available only on the validated Windows x86 game thread.
ScriptRegistryApi igiScriptRegistryApi(NativeCaller &caller) noexcept;
MenuScriptApi igiMenuScriptApi(NativeCaller &caller) noexcept;

#if defined(_WIN32) && (defined(__i386__) || defined(_M_IX86))
static_assert(sizeof(IgiScriptSymbol) == 28);
static_assert(offsetof(IgiScriptSymbol, handler) == 16);
static_assert(offsetof(IgiScriptSymbol, data) == 24);
#endif

bool menuScriptSymbolMatches(const IgiScriptSymbol *symbol, ScriptCallback callback,
                             void *context) noexcept;

// Must be called on the game thread after the game's script system is ready.
bool registerMenuScriptCallbacks(MenuScriptCallbacks &callbacks,
                                 ScriptRegistryApi registry) noexcept;
void unregisterMenuScriptCallbacks(MenuScriptCallbacks &callbacks,
                                   ScriptRegistryApi registry) noexcept;

class MenuScriptRegistration final {
  public:
    MenuScriptRegistration() = default;
    MenuScriptRegistration(const MenuScriptRegistration &) = delete;
    MenuScriptRegistration &operator=(const MenuScriptRegistration &) = delete;
    bool start(MenuScriptCallbacks &callbacks, ScriptRegistryApi registry) noexcept;
    void stop() noexcept;
    bool active() const noexcept {
        return active_;
    }

  private:
    ScriptRegistryApi registry_{};
    MenuScriptCallbacks *callbacks_ = nullptr;
    bool active_ = false;
};
} // namespace igi
