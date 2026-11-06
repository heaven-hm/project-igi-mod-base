#pragma once

#include "igi_sdk/ui/menu_values.hpp"

namespace igi {
class NativeCaller;
// Platform-neutral portion of the IGI script callbacks. The runtime adapter
// must invoke these on the game thread while the game is filling a list.
class MenuScriptBridge final {
public:
    using SelectionHandler = std::function<void(std::size_t, std::string_view)>;
    using CloseHandler = std::function<void()>;
    explicit MenuScriptBridge(const MenuConfig& config, SelectionHandler onSelection = {}, CloseHandler onClose = {})
        : values_(config), onSelection_(std::move(onSelection)), onClose_(std::move(onClose)) {}
    bool fillList(int item, const std::function<void(std::string_view, int)>& addItem) const;
    int getList(int item) const noexcept;
    bool setList(int item, int value);
    bool activate(int item);
    int getToggle(int item) const noexcept;
    bool setToggle(int item, int value);
    void close();

private:
    MenuValues values_;
    SelectionHandler onSelection_;
    CloseHandler onClose_;
};

// IGI 1 x86 ScriptValue layout, as recovered from the game interface.
struct IgiScriptValue {
    int integer;
    int reserved1;
    double real;
    const char* text;
    int reserved2;
};
#if defined(_WIN32) && (defined(__i386__) || defined(_M_IX86))
static_assert(sizeof(IgiScriptValue) == 24);
static_assert(offsetof(IgiScriptValue, real) == 8);
static_assert(offsetof(IgiScriptValue, text) == 16);
#endif

struct MenuScriptApi {
    using GetInt = int (*)(void*, int);
    using AddItem = void (*)(const char*, int);
    GetInt getInt = nullptr;
    AddItem addItem = nullptr;
};

class MenuScriptCallbacks final {
public:
    MenuScriptCallbacks(MenuScriptBridge& bridge, MenuScriptApi api) : bridge_(bridge), api_(api) {}
    IgiScriptValue* fill(IgiScriptValue* output, void* args);
    IgiScriptValue* get(IgiScriptValue* output, void* args);
    IgiScriptValue* set(IgiScriptValue* output, void* args);
    IgiScriptValue* getToggle(IgiScriptValue* output, void* args);
    IgiScriptValue* setToggle(IgiScriptValue* output, void* args);
    IgiScriptValue* action(IgiScriptValue* output, void* args);
    IgiScriptValue* close(IgiScriptValue* output, void* args);
private:
    MenuScriptBridge& bridge_;
    MenuScriptApi api_;
};

using ScriptCallback = IgiScriptValue* (*)(IgiScriptValue*, void*, void*);
struct ScriptRegistryApi {
    void (*registerFunction)(const char*, ScriptCallback, void*, int) = nullptr;
    void (*registerIntFunction)(const char*, ScriptCallback, void*, int) = nullptr;
    void (*removeSymbol)(const char*) = nullptr;
    bool (*containsSymbol)(const char*) = nullptr;
    bool (*matchesSymbol)(const char*, ScriptCallback, void*) = nullptr;
};

// Available only on the validated Windows x86 game thread.
ScriptRegistryApi igi1ScriptRegistryApi(NativeCaller& caller) noexcept;
MenuScriptApi igi1MenuScriptApi(NativeCaller& caller) noexcept;

// Minimal recovered IGI 1 symbol fields needed for ownership verification.
struct IgiScriptSymbol {
    int kind;
    int type;
    int dynamic;
    int size;
    ScriptCallback handler;
    const char* name;
    void* data;
};
#if defined(_WIN32) && (defined(__i386__) || defined(_M_IX86))
static_assert(sizeof(IgiScriptSymbol) == 28);
static_assert(offsetof(IgiScriptSymbol, handler) == 16);
static_assert(offsetof(IgiScriptSymbol, data) == 24);
#endif

bool menuScriptSymbolMatches(const IgiScriptSymbol* symbol, ScriptCallback callback, void* context) noexcept;

// Must be called on the game thread after the game's script system is ready.
bool registerMenuScriptCallbacks(MenuScriptCallbacks& callbacks, ScriptRegistryApi registry) noexcept;
void unregisterMenuScriptCallbacks(MenuScriptCallbacks& callbacks, ScriptRegistryApi registry) noexcept;

class MenuScriptRegistration final {
public:
    MenuScriptRegistration() = default;
    MenuScriptRegistration(const MenuScriptRegistration&) = delete;
    MenuScriptRegistration& operator=(const MenuScriptRegistration&) = delete;
    bool start(MenuScriptCallbacks& callbacks, ScriptRegistryApi registry) noexcept;
    void stop() noexcept;
    bool active() const noexcept { return active_; }
private:
    ScriptRegistryApi registry_{};
    MenuScriptCallbacks* callbacks_ = nullptr;
    bool active_ = false;
};
}
