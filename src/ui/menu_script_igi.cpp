#include "ui/menu_script_bridge.hpp"
#include "natives/native_caller.hpp"

namespace igi {
#if defined(_WIN32) && (defined(__i386__) || defined(_M_IX86))
namespace {
IgiScriptSymbol *findSymbol(const char *name) noexcept {
    using Find = const char *(__cdecl *)(const char *, IgiScriptSymbol **);
    IgiScriptSymbol *symbol = nullptr;
    return reinterpret_cast<Find>(0x004BAB80)(name, &symbol) ? symbol : nullptr;
}
bool contains(const char *name) {
    return findSymbol(name) != nullptr;
}
bool matches(const char *name, ScriptCallback callback, void *context) {
    return menuScriptSymbolMatches(findSymbol(name), callback, context);
}
} // namespace
#endif

ScriptRegistryApi igiScriptRegistryApi(NativeCaller &caller) noexcept {
#if defined(_WIN32) && (defined(__i386__) || defined(_M_IX86))
    if (!caller.targetValidated() || !caller.scheduler().isGameThread())
        return {};
    using Register = void(__cdecl *)(const char *, ScriptCallback, void *, int);
    using Remove = void(__cdecl *)(const char *);
    return {reinterpret_cast<Register>(0x004B8890), reinterpret_cast<Register>(0x004B88C0),
            reinterpret_cast<Remove>(0x004B8920), contains, matches};
#else
    (void)caller;
    return {};
#endif
}

MenuScriptApi igiMenuScriptApi(NativeCaller &caller) noexcept {
#if defined(_WIN32) && (defined(__i386__) || defined(_M_IX86))
    if (!caller.targetValidated() || !caller.scheduler().isGameThread())
        return {};
    return {reinterpret_cast<MenuScriptApi::GetInt>(0x004B8A20),
            reinterpret_cast<MenuScriptApi::AddItem>(0x0041F2D0)};
#else
    (void)caller;
    return {};
#endif
}
} // namespace igi
