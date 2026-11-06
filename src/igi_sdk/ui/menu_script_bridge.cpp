#include "igi_sdk/ui/menu_script_bridge.hpp"
#include "igi_sdk/logging/logging.hpp"

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

int MenuScriptBridge::getToggle(int item) const noexcept {
    if (item < 0) return -1;
    const auto value = values_.toggle(static_cast<std::size_t>(item));
    return value ? (*value ? 1 : 0) : -1;
}

bool MenuScriptBridge::setToggle(int item, int value) {
    if (item < 0 || (value != 0 && value != 1)) return false;
    const auto index = static_cast<std::size_t>(item);
    const auto previous = values_.toggle(index);
    if (!previous || !values_.setToggle(index, value != 0)) return false;
    if (onSelection_) {
        try {
            onSelection_(index, value ? "true" : "false");
        } catch (...) {
            values_.setToggle(index, *previous);
            throw;
        }
    }
    return true;
}

bool MenuScriptBridge::setList(int item, int value) {
    if (item < 0 || value < 0) return false;
    const auto previous = values_.selected(static_cast<std::size_t>(item));
    if (!previous || !values_.select(static_cast<std::size_t>(item), static_cast<std::size_t>(value))) return false;
    if (onSelection_) {
        const auto selected = values_.choice(static_cast<std::size_t>(item), static_cast<std::size_t>(value));
        try {
            if (selected) onSelection_(static_cast<std::size_t>(item), *selected);
        } catch (...) {
            values_.select(static_cast<std::size_t>(item), *previous);
            throw;
        }
    }
    return true;
}

bool MenuScriptBridge::activate(int item) {
    if (item < 0 || static_cast<std::size_t>(item) >= values_.itemCount()) return false;
    if (!onSelection_) return true;
    const auto index = static_cast<std::size_t>(item);
    if (const auto toggle = values_.toggle(index)) {
        if (!values_.setToggle(index, !*toggle)) return false;
        onSelection_(index, *toggle ? "false" : "true");
        return true;
    }
    const auto selected = values_.choice(index, 0);
    onSelection_(index, selected.value_or(std::string_view{}));
    return true;
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
IgiScriptValue* MenuScriptCallbacks::getToggle(IgiScriptValue* output, void* args) {
    if (!output || !args || !api_.getInt) return result(output, -1);
    return result(output, bridge_.getToggle(api_.getInt(args, 0)));
}
IgiScriptValue* MenuScriptCallbacks::setToggle(IgiScriptValue* output, void* args) {
    if (!output || !args || !api_.getInt) return result(output, 0);
    const bool ok = bridge_.setToggle(api_.getInt(args, 0), api_.getInt(args, 1));
    return result(output, ok ? 1 : 0);
}
IgiScriptValue* MenuScriptCallbacks::action(IgiScriptValue* output, void* args) {
    if (!output || !args || !api_.getInt) return result(output, 0);
    return result(output, bridge_.activate(api_.getInt(args, 0)) ? 1 : 0);
}

namespace {
IgiScriptValue* fillCallback(IgiScriptValue* out, void* context, void* args) {
    try {
        return context ? static_cast<MenuScriptCallbacks*>(context)->fill(out, args) : result(out, 0);
    } catch (...) {
        log(LogLevel::Error, "IGI_SDK_FillList: callback threw");
        return result(out, 0);
    }
}
IgiScriptValue* getCallback(IgiScriptValue* out, void* context, void* args) {
    try {
        return context ? static_cast<MenuScriptCallbacks*>(context)->get(out, args) : result(out, -1);
    } catch (...) {
        log(LogLevel::Error, "IGI_SDK_GetList: callback threw");
        return result(out, -1);
    }
}
IgiScriptValue* setCallback(IgiScriptValue* out, void* context, void* args) {
    try {
        return context ? static_cast<MenuScriptCallbacks*>(context)->set(out, args) : result(out, 0);
    } catch (...) {
        log(LogLevel::Error, "IGI_SDK_SetList: callback threw");
        return result(out, 0);
    }
}
IgiScriptValue* getToggleCallback(IgiScriptValue* out, void* context, void* args) {
    try {
        return context ? static_cast<MenuScriptCallbacks*>(context)->getToggle(out, args) : result(out, -1);
    } catch (...) {
        log(LogLevel::Error, "IGI_SDK_GetToggle: callback threw");
        return result(out, -1);
    }
}
IgiScriptValue* setToggleCallback(IgiScriptValue* out, void* context, void* args) {
    try {
        return context ? static_cast<MenuScriptCallbacks*>(context)->setToggle(out, args) : result(out, 0);
    } catch (...) {
        log(LogLevel::Error, "IGI_SDK_SetToggle: callback threw");
        return result(out, 0);
    }
}
IgiScriptValue* actionCallback(IgiScriptValue* out, void* context, void* args) {
    try {
        return context ? static_cast<MenuScriptCallbacks*>(context)->action(out, args) : result(out, 0);
    } catch (...) {
        log(LogLevel::Error, "IGI_SDK_Action: callback threw");
        return result(out, 0);
    }
}
}

bool registerMenuScriptCallbacks(MenuScriptCallbacks& callbacks, ScriptRegistryApi registry) noexcept {
    if (!registry.registerFunction || !registry.registerIntFunction || !registry.removeSymbol || !registry.containsSymbol || !registry.matchesSymbol) return false;
    if (registry.containsSymbol("IGI_SDK_FillList") || registry.containsSymbol("IGI_SDK_GetList") ||
        registry.containsSymbol("IGI_SDK_SetList") || registry.containsSymbol("IGI_SDK_GetToggle") ||
        registry.containsSymbol("IGI_SDK_SetToggle") || registry.containsSymbol("IGI_SDK_Action")) return false;
    registry.registerFunction("IGI_SDK_FillList", fillCallback, &callbacks, 1);
    registry.registerIntFunction("IGI_SDK_GetList", getCallback, &callbacks, 1);
    registry.registerFunction("IGI_SDK_SetList", setCallback, &callbacks, 2);
    registry.registerIntFunction("IGI_SDK_GetToggle", getToggleCallback, &callbacks, 1);
    registry.registerFunction("IGI_SDK_SetToggle", setToggleCallback, &callbacks, 2);
    registry.registerFunction("IGI_SDK_Action", actionCallback, &callbacks, 1);
    return true;
}

void unregisterMenuScriptCallbacks(MenuScriptCallbacks& callbacks, ScriptRegistryApi registry) noexcept {
    if (!registry.removeSymbol || !registry.matchesSymbol) return;
    // IGI's delete routine loops forever on missing names; never remove a foreign symbol.
    if (registry.matchesSymbol("IGI_SDK_FillList", fillCallback, &callbacks)) registry.removeSymbol("IGI_SDK_FillList");
    if (registry.matchesSymbol("IGI_SDK_GetList", getCallback, &callbacks)) registry.removeSymbol("IGI_SDK_GetList");
    if (registry.matchesSymbol("IGI_SDK_SetList", setCallback, &callbacks)) registry.removeSymbol("IGI_SDK_SetList");
    if (registry.matchesSymbol("IGI_SDK_GetToggle", getToggleCallback, &callbacks)) registry.removeSymbol("IGI_SDK_GetToggle");
    if (registry.matchesSymbol("IGI_SDK_SetToggle", setToggleCallback, &callbacks)) registry.removeSymbol("IGI_SDK_SetToggle");
    if (registry.matchesSymbol("IGI_SDK_Action", actionCallback, &callbacks)) registry.removeSymbol("IGI_SDK_Action");
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
