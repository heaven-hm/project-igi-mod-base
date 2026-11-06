#include "igi_sdk/ui/menu_script_bridge.hpp"

namespace igi {
bool menuScriptSymbolMatches(const IgiScriptSymbol* symbol, ScriptCallback callback, void* context) noexcept {
    return symbol && symbol->kind == 1 && symbol->handler == callback && symbol->data == context;
}

bool MenuScriptBridge::fillList(int item, const std::function<void(std::string_view, int)>& addItem) const {
    return item >= 0 && values_.fill(static_cast<std::size_t>(item), addItem);
}

int MenuScriptBridge::getList(int item) const noexcept {
    if (item < 0) return -1;
    const auto value = values_.selected(static_cast<std::size_t>(item));
    return value ? static_cast<int>(*value) : -1;
}

bool MenuScriptBridge::setList(int item, int value) noexcept {
    return item >= 0 && value >= 0 && values_.select(static_cast<std::size_t>(item), static_cast<std::size_t>(value));
}

namespace {
IgiScriptValue* result(IgiScriptValue* output, int value) noexcept {
    if (!output) return nullptr;
    output->integer = value;
    output->real = static_cast<double>(value);
    output->text = "";
    return output;
}
}

IgiScriptValue* MenuScriptCallbacks::fill(IgiScriptValue* output, void* args) {
    if (!output || !args || !api_.getInt || !api_.addItem) return result(output, 0);
    const int item = api_.getInt(args, 0);
    const bool ok = bridge_.fillList(item, [this](std::string_view label, int id) {
        // Labels are owned by MenuValues and are null-terminated std::strings.
        api_.addItem(label.data(), id);
    });
    return result(output, ok ? 1 : 0);
}

IgiScriptValue* MenuScriptCallbacks::get(IgiScriptValue* output, void* args) {
    if (!output || !args || !api_.getInt) return result(output, -1);
    return result(output, bridge_.getList(api_.getInt(args, 0)));
}

IgiScriptValue* MenuScriptCallbacks::set(IgiScriptValue* output, void* args) {
    if (!output || !args || !api_.getInt) return result(output, 0);
    return result(output, bridge_.setList(api_.getInt(args, 0), api_.getInt(args, 1)) ? 1 : 0);
}

namespace {
IgiScriptValue* fillCallback(IgiScriptValue* out, void* context, void* args) {
    return context ? static_cast<MenuScriptCallbacks*>(context)->fill(out, args) : nullptr;
}
IgiScriptValue* getCallback(IgiScriptValue* out, void* context, void* args) {
    return context ? static_cast<MenuScriptCallbacks*>(context)->get(out, args) : nullptr;
}
IgiScriptValue* setCallback(IgiScriptValue* out, void* context, void* args) {
    return context ? static_cast<MenuScriptCallbacks*>(context)->set(out, args) : nullptr;
}
}

bool registerMenuScriptCallbacks(MenuScriptCallbacks& callbacks, ScriptRegistryApi registry) noexcept {
    if (!registry.registerFunction || !registry.registerIntFunction || !registry.removeSymbol || !registry.containsSymbol || !registry.matchesSymbol) return false;
    if (registry.containsSymbol("IGI_SDK_FillList") || registry.containsSymbol("IGI_SDK_GetList") ||
        registry.containsSymbol("IGI_SDK_SetList")) return false;
    registry.registerFunction("IGI_SDK_FillList", fillCallback, &callbacks, 1);
    registry.registerIntFunction("IGI_SDK_GetList", getCallback, &callbacks, 1);
    registry.registerFunction("IGI_SDK_SetList", setCallback, &callbacks, 2);
    return true;
}

void unregisterMenuScriptCallbacks(MenuScriptCallbacks& callbacks, ScriptRegistryApi registry) noexcept {
    if (!registry.removeSymbol || !registry.matchesSymbol) return;
    // IGI's delete routine loops forever on missing names; never remove a foreign symbol.
    if (registry.matchesSymbol("IGI_SDK_FillList", fillCallback, &callbacks)) registry.removeSymbol("IGI_SDK_FillList");
    if (registry.matchesSymbol("IGI_SDK_GetList", getCallback, &callbacks)) registry.removeSymbol("IGI_SDK_GetList");
    if (registry.matchesSymbol("IGI_SDK_SetList", setCallback, &callbacks)) registry.removeSymbol("IGI_SDK_SetList");
}

bool MenuScriptRegistration::start(MenuScriptCallbacks& callbacks, ScriptRegistryApi registry) noexcept {
    if (active_ || !registerMenuScriptCallbacks(callbacks, registry)) return false;
    registry_ = registry;
    callbacks_ = &callbacks;
    active_ = true;
    return true;
}

void MenuScriptRegistration::stop() noexcept {
    if (!active_) return;
    unregisterMenuScriptCallbacks(*callbacks_, registry_);
    registry_ = {};
    callbacks_ = nullptr;
    active_ = false;
}
}
