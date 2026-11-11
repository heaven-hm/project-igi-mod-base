#include "ui/menu_script_bridge.hpp"
#include "natives/mvp_natives.hpp"
#include "logging/logging.hpp"
#include "natives/igi_native_registry.hpp"
#include "natives/igi_hashes/hashes.hpp"

namespace igi {
bool menuScriptSymbolMatches(const IgiScriptSymbol *symbol, ScriptCallback callback,
                             void *context) noexcept {
    return symbol && symbol->kind == 1 && symbol->handler == callback && symbol->data == context;
}

bool MenuScriptBridge::fillList(int item,
                                const std::function<void(std::string_view, int)> &addItem) {
    if (item == kCommandList) {
        if (commands_.empty() || !addItem)
            return false;
        if (commandPage_ >= commandPageCount())
            commandPage_ = commandPageCount() - 1;
        const std::size_t first = commandPage_ * kCommandsPerPage;
        const std::size_t last = std::min(first + kCommandsPerPage, commands_.size());
        int id = 0;
        for (std::size_t index = first; index < last; ++index)
            addItem(commands_[index].label, id++);
        if (commandPageCount() > 1) {
            addItem(commandPage_ + 1 < commandPageCount() ? "Next page >" : "First page", id++);
            addItem(commandPage_ > 0 ? "< Previous page" : "Last page", id);
        }
        return true;
    }
    return item >= 0 && values_.fill(static_cast<std::size_t>(item), addItem);
}

bool MenuScriptBridge::resync(const MenuConfig &config) {
    const bool valuesChanged = values_.resync(config);
    auto rebuilt = buildMenuCommands(config);
    const bool commandsChanged = rebuilt != commands_;
    commands_ = std::move(rebuilt);
    if (selectedCommand_ && *selectedCommand_ >= commands_.size())
        selectedCommand_.reset();
    if (commandPage_ >= commandPageCount())
        commandPage_ = commandPageCount() > 0 ? commandPageCount() - 1 : 0;
    return valuesChanged || commandsChanged;
}

void MenuScriptBridge::refreshScreen() {
#if defined(_WIN32) && (defined(__i386__) || defined(_M_IX86))
    if (screenId_ < 0 || igiNativeCaller() == nullptr)
        return;
    auto *screen = IGI::TASK::FIND_BY_ID(screenId_);
    if (!screen)
        return;
    using GetChangedEvent = std::uint8_t(__cdecl *)();
    using Broadcast = void(__cdecl *)(IGI::Task *, std::uint8_t, void *);
    const auto changedEvent =
        reinterpret_cast<GetChangedEvent>(igi::re::HASH::Param_GetChangedEvent)();
    char changed = 1;
    reinterpret_cast<Broadcast>(igi::re::HASH::Task_Broadcast)(screen, changedEvent, &changed);
#endif
}

bool MenuScriptBridge::runSelectedCommand() {
    if (!selectedCommand_ || *selectedCommand_ >= commands_.size() || !onSelection_)
        return false;
    const auto &command = commands_[*selectedCommand_];
    onSelection_(command.itemIndex, command.value);
    return true;
}

int MenuScriptBridge::getList(int item) const noexcept {
    if (item == kCommandList) {
        if (!selectedCommand_)
            return -1;
        const std::size_t first = commandPage_ * kCommandsPerPage;
        return *selectedCommand_ >= first && *selectedCommand_ < first + kCommandsPerPage
                   ? static_cast<int>(*selectedCommand_ - first)
                   : -1;
    }
    if (item < 0)
        return -1;
    const auto value = values_.selected(static_cast<std::size_t>(item));
    return value ? static_cast<int>(*value) : -1;
}

int MenuScriptBridge::getToggle(int item) const noexcept {
    if (item < 0)
        return -1;
    const auto value = values_.toggle(static_cast<std::size_t>(item));
    return value ? (*value ? 1 : 0) : -1;
}

bool MenuScriptBridge::setToggle(int item, int value) {
    if (item < 0 || (value != 0 && value != 1))
        return false;
    const auto index = static_cast<std::size_t>(item);
    const auto previous = values_.toggle(index);
    if (!previous || !values_.setToggle(index, value != 0))
        return false;
    if (!deferUntilApply_ && onSelection_) {
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
    if (item == kCommandList) {
        if (value < 0)
            return false;
        const std::size_t first = commandPage_ * kCommandsPerPage;
        const std::size_t visible = std::min(kCommandsPerPage, commands_.size() - first);
        if (static_cast<std::size_t>(value) >= visible) {
            // Page navigation: the retail list is repopulated in place, so no
            // screen teardown or menu transition is involved.
            const bool next = static_cast<std::size_t>(value) == visible;
            if (commandPageCount() > 1)
                commandPage_ = next ? (commandPage_ + 1) % commandPageCount()
                                    : (commandPage_ + commandPageCount() - 1) % commandPageCount();
            selectedCommand_.reset();
            refreshScreen();
            return true;
        }
        selectedCommand_ = first + static_cast<std::size_t>(value);
        // The retail ListBox reports every selection change, so an immediate
        // frontend runs the entry right away while a deferred one waits for
        // Apply and keeps the selection visible for the following frame.
        if (!deferUntilApply_)
            return runSelectedCommand();
        return true;
    }
    if (item < 0 || value < 0)
        return false;
    const auto previous = values_.selected(static_cast<std::size_t>(item));
    if (!previous ||
        !values_.select(static_cast<std::size_t>(item), static_cast<std::size_t>(value)))
        return false;
    if (!deferUntilApply_ && onSelection_) {
        const auto selected =
            values_.choice(static_cast<std::size_t>(item), static_cast<std::size_t>(value));
        try {
            if (selected)
                onSelection_(static_cast<std::size_t>(item), *selected);
        } catch (...) {
            values_.select(static_cast<std::size_t>(item), *previous);
            throw;
        }
    }
    return true;
}

bool MenuScriptBridge::activate(int item) {
    if (item < 0 || static_cast<std::size_t>(item) >= values_.itemCount())
        return false;
    if (!onSelection_)
        return true;
    const auto index = static_cast<std::size_t>(item);
    if (const auto toggle = values_.toggle(index)) {
        if (!values_.setToggle(index, !*toggle))
            return false;
        if (!deferUntilApply_) {
            onSelection_(index, *toggle ? "false" : "true");
        }
        return true;
    }
    const auto selected = values_.choice(index, 0);
    onSelection_(index, selected.value_or(std::string_view{}));
    return true;
}

void MenuScriptBridge::page(int delta) {
    if (delta != 0 && onPage_)
        onPage_(delta);
}

void MenuScriptBridge::restart() {
    if (onRestart_)
        onRestart_();
}

void MenuScriptBridge::close() {
    if (onClose_)
        onClose_();
}

void MenuScriptBridge::apply() {
#if defined(_WIN32) && (defined(__i386__) || defined(_M_IX86))
    if (screenId_ >= 0 && igiNativeCaller() != nullptr) {
        if (auto *screenTask = IGI::TASK::FIND_BY_ID(screenId_)) {
            using GetSaveEvent = std::uint8_t(__cdecl *)();
            using Broadcast = void(__cdecl *)(IGI::Task *, std::uint8_t, void *);
            const auto saveEvent =
                reinterpret_cast<GetSaveEvent>(igi::re::HASH::MenuWindow_GetSaveEvent)();
            reinterpret_cast<Broadcast>(igi::re::HASH::Task_Broadcast)(screenTask, saveEvent,
                                                                       nullptr);
        }
    }
#endif
    if (deferUntilApply_ && selectedCommand_ && runSelectedCommand()) {
        values_.commit();
        if (onClose_)
            onClose_();
        return;
    }
    if (deferUntilApply_ && onSelection_) {
        const auto count = values_.itemCount();
        for (std::size_t i = 0; i < count; ++i) {
            if (values_.toggleChanged(i)) {
                if (const auto toggle = values_.toggle(i)) {
                    onSelection_(i, *toggle ? "true" : "false");
                }
            } else if (values_.selectedChanged(i)) {
                if (const auto selected = values_.selected(i)) {
                    if (const auto choice = values_.choice(i, *selected)) {
                        onSelection_(i, *choice);
                    }
                }
            }
        }
    }
    values_.commit();
    if (onClose_)
        onClose_();
}

void MenuScriptBridge::cancel(bool notifyClose) {
    values_.revert();
    selectedCommand_.reset();
    if (notifyClose && onClose_)
        onClose_();
}

namespace {
IgiScriptValue *result(IgiScriptValue *output, int value) noexcept {
    if (!output)
        return nullptr;
    output->integer = value;
    output->real = static_cast<double>(value);
    output->text = "";
    return output;
}
} // namespace

IgiScriptValue *MenuScriptCallbacks::fill(IgiScriptValue *output, void *args) {
    if (!output || !args || !api_.getInt || !api_.addItem)
        return result(output, 0);
    const int item = api_.getInt(args, 0);
    const bool ok = bridge_.fillList(item, [this](std::string_view label, int id) {
        // Labels are owned by MenuValues and are null-terminated std::strings.
        api_.addItem(label.data(), id);
    });
    return result(output, ok ? 1 : 0);
}

IgiScriptValue *MenuScriptCallbacks::get(IgiScriptValue *output, void *args) {
    if (!output || !args || !api_.getInt)
        return result(output, -1);
    return result(output, bridge_.getList(api_.getInt(args, 0)));
}

IgiScriptValue *MenuScriptCallbacks::set(IgiScriptValue *output, void *args) {
    if (!output || !args || !api_.getInt)
        return result(output, 0);
    return result(output, bridge_.setList(api_.getInt(args, 0), api_.getInt(args, 1)) ? 1 : 0);
}
IgiScriptValue *MenuScriptCallbacks::getToggle(IgiScriptValue *output, void *args) {
    if (!output || !args || !api_.getInt)
        return result(output, -1);
    return result(output, bridge_.getToggle(api_.getInt(args, 0)));
}
IgiScriptValue *MenuScriptCallbacks::setToggle(IgiScriptValue *output, void *args) {
    if (!output || !args || !api_.getInt)
        return result(output, 0);
    const bool ok = bridge_.setToggle(api_.getInt(args, 0), api_.getInt(args, 1));
    return result(output, ok ? 1 : 0);
}
IgiScriptValue *MenuScriptCallbacks::action(IgiScriptValue *output, void *args) {
    if (!output || !args || !api_.getInt)
        return result(output, 0);
    return result(output, bridge_.activate(api_.getInt(args, 0)) ? 1 : 0);
}
IgiScriptValue *MenuScriptCallbacks::page(IgiScriptValue *output, void *args) {
    if (!output || !args || !api_.getInt)
        return result(output, 0);
    bridge_.page(api_.getInt(args, 0));
    return result(output, 1);
}
IgiScriptValue *MenuScriptCallbacks::close(IgiScriptValue *output, void *) {
    bridge_.close();
    return result(output, 1);
}
IgiScriptValue *MenuScriptCallbacks::apply(IgiScriptValue *output, void *) {
    bridge_.apply();
    return result(output, 1);
}
IgiScriptValue *MenuScriptCallbacks::cancel(IgiScriptValue *output, void *) {
    bridge_.cancel();
    return result(output, 1);
}

IgiScriptValue *MenuScriptCallbacks::restart(IgiScriptValue *output, void *) {
    bridge_.restart();
    return result(output, 1);
}

namespace {
IgiScriptValue *fillCallback(IgiScriptValue *out, void *context, void *args) {
    try {
        return context ? static_cast<MenuScriptCallbacks *>(context)->fill(out, args)
                       : result(out, 0);
    } catch (...) {
        log(LogLevel::Error, "IGI_SDK_FillList: callback threw");
        return result(out, 0);
    }
}
IgiScriptValue *getCallback(IgiScriptValue *out, void *context, void *args) {
    try {
        return context ? static_cast<MenuScriptCallbacks *>(context)->get(out, args)
                       : result(out, -1);
    } catch (...) {
        log(LogLevel::Error, "IGI_SDK_GetList: callback threw");
        return result(out, -1);
    }
}
IgiScriptValue *setCallback(IgiScriptValue *out, void *context, void *args) {
    try {
        return context ? static_cast<MenuScriptCallbacks *>(context)->set(out, args)
                       : result(out, 0);
    } catch (...) {
        log(LogLevel::Error, "IGI_SDK_SetList: callback threw");
        return result(out, 0);
    }
}
IgiScriptValue *getToggleCallback(IgiScriptValue *out, void *context, void *args) {
    try {
        return context ? static_cast<MenuScriptCallbacks *>(context)->getToggle(out, args)
                       : result(out, -1);
    } catch (...) {
        log(LogLevel::Error, "IGI_SDK_GetToggle: callback threw");
        return result(out, -1);
    }
}
IgiScriptValue *setToggleCallback(IgiScriptValue *out, void *context, void *args) {
    try {
        return context ? static_cast<MenuScriptCallbacks *>(context)->setToggle(out, args)
                       : result(out, 0);
    } catch (...) {
        log(LogLevel::Error, "IGI_SDK_SetToggle: callback threw");
        return result(out, 0);
    }
}
IgiScriptValue *actionCallback(IgiScriptValue *out, void *context, void *args) {
    try {
        return context ? static_cast<MenuScriptCallbacks *>(context)->action(out, args)
                       : result(out, 0);
    } catch (...) {
        log(LogLevel::Error, "IGI_SDK_Action: callback threw");
        return result(out, 0);
    }
}
IgiScriptValue *pageCallback(IgiScriptValue *out, void *context, void *args) {
    try {
        return context ? static_cast<MenuScriptCallbacks *>(context)->page(out, args)
                       : result(out, 0);
    } catch (...) {
        log(LogLevel::Error, "IGI_SDK_Page: callback threw");
        return result(out, 0);
    }
}
IgiScriptValue *closeCallback(IgiScriptValue *out, void *context, void *args) {
    try {
        return context ? static_cast<MenuScriptCallbacks *>(context)->close(out, args)
                       : result(out, 0);
    } catch (...) {
        log(LogLevel::Error, "IGI_SDK_CloseMenu: callback threw");
        return result(out, 0);
    }
}
IgiScriptValue *applyCallback(IgiScriptValue *out, void *context, void *args) {
    try {
        return context ? static_cast<MenuScriptCallbacks *>(context)->apply(out, args)
                       : result(out, 0);
    } catch (...) {
        log(LogLevel::Error, "IGI_SDK_Apply: callback threw");
        return result(out, 0);
    }
}
IgiScriptValue *cancelCallback(IgiScriptValue *out, void *context, void *args) {
    try {
        return context ? static_cast<MenuScriptCallbacks *>(context)->cancel(out, args)
                       : result(out, 0);
    } catch (...) {
        log(LogLevel::Error, "IGI_SDK_Cancel: callback threw");
        return result(out, 0);
    }
}

IgiScriptValue *restartCallback(IgiScriptValue *out, void *context, void *args) {
    try {
        return context ? static_cast<MenuScriptCallbacks *>(context)->restart(out, args)
                       : result(out, 0);
    } catch (...) {
        log(LogLevel::Error, "IGI_SDK_Restart: callback threw");
        return result(out, 0);
    }
}
} // namespace

namespace {
// Every IGI_SDK_* trampoline this module can register, so a stale entry can be
// recognised by its handler even when its context object is long gone.
struct OwnScriptSymbol {
    const char *name;
    ScriptCallback handler;
};
bool isOwnScriptHandler(ScriptCallback handler) noexcept {
    return handler == fillCallback || handler == getCallback || handler == setCallback ||
           handler == getToggleCallback || handler == setToggleCallback ||
           handler == actionCallback || handler == closeCallback || handler == applyCallback ||
           handler == cancelCallback || handler == pageCallback || handler == restartCallback;
}
constexpr OwnScriptSymbol kOwnScriptSymbols[] = {
    {"IGI_SDK_FillList", fillCallback},       {"IGI_SDK_GetList", getCallback},
    {"IGI_SDK_SetList", setCallback},         {"IGI_SDK_GetToggle", getToggleCallback},
    {"IGI_SDK_SetToggle", setToggleCallback}, {"IGI_SDK_Action", actionCallback},
    {"IGI_SDK_CloseMenu", closeCallback},     {"IGI_SDK_Apply", applyCallback},
    {"IGI_SDK_Cancel", cancelCallback},       {"IGI_SDK_Page", pageCallback},
    {"IGI_SDK_Restart", restartCallback},
};
} // namespace

bool registerMenuScriptCallbacks(MenuScriptCallbacks &callbacks,
                                 ScriptRegistryApi registry) noexcept {
    if (!registry.registerFunction || !registry.registerIntFunction || !registry.removeSymbol ||
        !registry.containsSymbol || !registry.matchesSymbol)
        return false;
    // Our own names. A level restart (or a game restart that reuses the process)
    // can leave the previous session's symbols in the retail script table: the
    // handler is one of our trampolines but the MenuScriptCallbacks context it
    // points at was destroyed. The screen then runs against that stale entry and
    // fills its ListBox with nothing - the "frame opens with no options" report.
    // Reclaim only our own entries; a foreign mod's symbol is never removed.
    if (!registry.findSymbol) {
        // Without symbol introspection ownership cannot be proven, so a name that
        // already exists is never registered over.
        for (const auto &entry : kOwnScriptSymbols) {
            if (registry.containsSymbol(entry.name))
                return false;
        }
    } else {
        for (const auto &entry : kOwnScriptSymbols) {
            const auto name = entry.name;
            if (!registry.containsSymbol(name))
                continue;
            const IgiScriptSymbol *symbol = registry.findSymbol(name);
            if (symbol && symbol->handler == entry.handler && symbol->data == &callbacks)
                continue; // this session's own live registration
            if (!symbol || !isOwnScriptHandler(symbol->handler)) {
                log(LogLevel::Error, std::string("IGI_SDK script symbol '") + name +
                                         "' belongs to another module; refusing to register "
                                         "over it");
                return false;
            }
            registry.removeSymbol(name);
            log(LogLevel::Info, std::string("IGI_SDK script symbol '") + name +
                                    "' was left by the previous menu session; re-registering");
        }
    }
    registry.registerFunction("IGI_SDK_FillList", fillCallback, &callbacks, 1);
    registry.registerIntFunction("IGI_SDK_GetList", getCallback, &callbacks, 1);
    registry.registerFunction("IGI_SDK_SetList", setCallback, &callbacks, 2);
    registry.registerIntFunction("IGI_SDK_GetToggle", getToggleCallback, &callbacks, 1);
    registry.registerFunction("IGI_SDK_SetToggle", setToggleCallback, &callbacks, 2);
    registry.registerFunction("IGI_SDK_Action", actionCallback, &callbacks, 1);
    registry.registerFunction("IGI_SDK_CloseMenu", closeCallback, &callbacks, 0);
    registry.registerFunction("IGI_SDK_Apply", applyCallback, &callbacks, 0);
    registry.registerFunction("IGI_SDK_Cancel", cancelCallback, &callbacks, 0);
    registry.registerFunction("IGI_SDK_Page", pageCallback, &callbacks, 1);
    registry.registerFunction("IGI_SDK_Restart", restartCallback, &callbacks, 0);
    return true;
}

void unregisterMenuScriptCallbacks(MenuScriptCallbacks &callbacks,
                                   ScriptRegistryApi registry) noexcept {
    if (!registry.removeSymbol || !registry.matchesSymbol)
        return;
    // IGI's delete routine loops forever on missing names; never remove a foreign symbol.
    if (registry.matchesSymbol("IGI_SDK_FillList", fillCallback, &callbacks))
        registry.removeSymbol("IGI_SDK_FillList");
    if (registry.matchesSymbol("IGI_SDK_GetList", getCallback, &callbacks))
        registry.removeSymbol("IGI_SDK_GetList");
    if (registry.matchesSymbol("IGI_SDK_SetList", setCallback, &callbacks))
        registry.removeSymbol("IGI_SDK_SetList");
    if (registry.matchesSymbol("IGI_SDK_GetToggle", getToggleCallback, &callbacks))
        registry.removeSymbol("IGI_SDK_GetToggle");
    if (registry.matchesSymbol("IGI_SDK_SetToggle", setToggleCallback, &callbacks))
        registry.removeSymbol("IGI_SDK_SetToggle");
    if (registry.matchesSymbol("IGI_SDK_Action", actionCallback, &callbacks))
        registry.removeSymbol("IGI_SDK_Action");
    if (registry.matchesSymbol("IGI_SDK_Page", pageCallback, &callbacks))
        registry.removeSymbol("IGI_SDK_Page");
    if (registry.matchesSymbol("IGI_SDK_CloseMenu", closeCallback, &callbacks))
        registry.removeSymbol("IGI_SDK_CloseMenu");
    if (registry.matchesSymbol("IGI_SDK_Apply", applyCallback, &callbacks))
        registry.removeSymbol("IGI_SDK_Apply");
    if (registry.matchesSymbol("IGI_SDK_Cancel", cancelCallback, &callbacks))
        registry.removeSymbol("IGI_SDK_Cancel");
    if (registry.matchesSymbol("IGI_SDK_Restart", restartCallback, &callbacks))
        registry.removeSymbol("IGI_SDK_Restart");
}

bool MenuScriptRegistration::start(MenuScriptCallbacks &callbacks,
                                   ScriptRegistryApi registry) noexcept {
    if (active_ || !registerMenuScriptCallbacks(callbacks, registry))
        return false;
    registry_ = registry;
    callbacks_ = &callbacks;
    active_ = true;
    return true;
}

void MenuScriptRegistration::stop() noexcept {
    if (!active_)
        return;
    unregisterMenuScriptCallbacks(*callbacks_, registry_);
    registry_ = {};
    callbacks_ = nullptr;
    active_ = false;
}
} // namespace igi
