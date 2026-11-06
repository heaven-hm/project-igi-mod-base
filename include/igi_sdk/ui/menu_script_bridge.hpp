#pragma once

#include "igi_sdk/ui/menu_values.hpp"

namespace igi {
// Platform-neutral portion of the IGI script callbacks. The runtime adapter
// must invoke these on the game thread while the game is filling a list.
class MenuScriptBridge final {
public:
    explicit MenuScriptBridge(const MenuConfig& config) : values_(config) {}
    bool fillList(int item, const std::function<void(std::string_view, int)>& addItem) const;
    int getList(int item) const noexcept;
    bool setList(int item, int value) noexcept;

private:
    MenuValues values_;
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
