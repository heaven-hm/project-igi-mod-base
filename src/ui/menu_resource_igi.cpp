#include "ui/menu_resource.hpp"
#include "natives/native_caller.hpp"

namespace igi {
MenuResourceApi igiMenuResourceApi(NativeCaller &caller) noexcept {
#if defined(_WIN32) && (defined(__i386__) || defined(_M_IX86))
    if (!caller.targetValidated() || !caller.scheduler().isGameThread())
        return {};
    // IGI-RE src/res/resource.c: Resource_IsLoaded, Resource_Register,
    // Resource_Free. Resource_Free removes RES_USER without freeing data.
    using IsLoaded = int(__cdecl *)(const char *, void **);
    using Register = void *(__cdecl *)(const char *, int, void *);
    using Free = void(__cdecl *)(void *);
    return {reinterpret_cast<IsLoaded>(0x004B5B90), reinterpret_cast<Register>(0x004B6420),
            reinterpret_cast<Free>(0x004B5C90)};
#else
    (void)caller;
    return {};
#endif
}
} // namespace igi
