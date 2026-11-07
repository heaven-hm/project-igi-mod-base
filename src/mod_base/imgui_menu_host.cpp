#include "config/menu_config.hpp"
#include "logging/logging.hpp"
#include "mod_base/menu_host.hpp"
#include "natives/igi_hashes/hashes.hpp"
#include "natives/igi_native_registry.hpp"
#include "natives/mvp_natives.hpp"
#include "platform/windows_dll.hpp"
#include "runtime/runtime.hpp"
#include "ui/menu_lifecycle.hpp"
#include "ui/menu_manager.hpp"
#include "ui/mod_menu.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#include <ddraw.h>
#include <d3d.h>

#include "imgui.h"
#include "imgui_impl_dx7.h"
#include "imgui_impl_win32.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam,
                                                             LPARAM lParam);
#endif

#if defined(_WIN32)
#define IGI_DEMO_EXPORT __declspec(dllexport)
#elif defined(__GNUC__) || defined(__clang__)
#define IGI_DEMO_EXPORT __attribute__((visibility("default")))
#else
#define IGI_DEMO_EXPORT
#endif

namespace {

#if defined(_WIN32)
struct DisplayCallbackNode {
    DisplayCallbackNode *next = nullptr;
    void(__cdecl *callback)() = nullptr;
};
#endif

struct ExternalImGuiMenuState {
    igi::Runtime runtime;
    std::unique_ptr<igi::MenuManager> menuModel;
    igi::MenuConfig menuConfig;
    std::vector<int> listSelections;
    std::vector<bool> toggleValues;
    std::vector<int> sliderValues;
    std::string lastStatusMessage;
    std::atomic<std::uint64_t> frameCount{0};
    std::atomic<bool> safeToShutdown{true};
    std::atomic<bool> shutdownPending{false};
    std::atomic<bool> initialAttachPending{false};
    bool menuVisible = false;
    bool pauseWhileOpen = true;
#if defined(_WIN32)
    IDirect3DDevice7 *boundDevice = nullptr;
    IDirectDraw7 *boundDDraw = nullptr;
    HWND boundHwnd = nullptr;
    WNDPROC originalWndProc = nullptr;
    bool imguiInitialized = false;
    DisplayCallbackNode endFrameNode{};
#endif
};

ExternalImGuiMenuState g_menu;
auto &runtime = g_menu.runtime;
auto &menuModel = g_menu.menuModel;
auto &menuConfig = g_menu.menuConfig;
auto &listSelections = g_menu.listSelections;
auto &toggleValues = g_menu.toggleValues;
auto &sliderValues = g_menu.sliderValues;
auto &lastStatusMessage = g_menu.lastStatusMessage;
auto &frameCount = g_menu.frameCount;
auto &safeToShutdown = g_menu.safeToShutdown;
auto &shutdownPending = g_menu.shutdownPending;
auto &initialAttachPending = g_menu.initialAttachPending;
auto &menuVisible = g_menu.menuVisible;
auto &pauseWhileOpen = g_menu.pauseWhileOpen;

void initializeMenuStateBuffers() {
    listSelections.assign(menuConfig.items.size(), 0);
    toggleValues.resize(menuConfig.items.size(), false);
    sliderValues.assign(menuConfig.items.size(), 0);
    for (std::size_t index = 0; index < menuConfig.items.size(); ++index) {
        toggleValues[index] = menuConfig.items[index].defaultToggle;
    }
    pauseWhileOpen = menuConfig.imgui.pauseGame;
    lastStatusMessage = "Ready";
}

bool populatePortableMenuModel() {
    menuModel = std::make_unique<igi::MenuManager>(runtime.scheduler());
    if (!menuModel->createMenu("main", menuConfig.title))
        return false;

    for (std::size_t index = 0; index < menuConfig.items.size(); ++index) {
        const auto &item = menuConfig.items[index];
        switch (item.kind) {
        case igi::ModMenuItemKind::List:
            if (!menuModel->addList(
                    "main", item.label, item.values, 0, [index](std::size_t selectedIndex) {
                        if (index < menuConfig.items.size() &&
                            selectedIndex < menuConfig.items[index].values.size()) {
                            runModMenu(menuConfig, runtime, index,
                                       menuConfig.items[index].values[selectedIndex]);
                        }
                    })) {
                return false;
            }
            break;
        case igi::ModMenuItemKind::Toggle:
            if (!menuModel->addToggle(
                    "main", item.label, item.defaultToggle, [index](bool enabled) {
                        runModMenu(menuConfig, runtime, index, enabled ? "true" : "false");
                    })) {
                return false;
            }
            break;
        case igi::ModMenuItemKind::Slider:
            if (!menuModel->addInteger("main", item.label, 0, 0, 100, 1, [index](int value) {
                    runModMenu(menuConfig, runtime, index, std::to_string(value));
                })) {
                return false;
            }
            break;
        case igi::ModMenuItemKind::Action:
            if (!menuModel->addAction("main", item.label,
                                      [index] { runModMenu(menuConfig, runtime, index, ""); })) {
                return false;
            }
            break;
        }
    }
    return true;
}

void queueMenuAction(std::size_t itemIndex, std::string value) {
    if (itemIndex >= menuConfig.items.size())
        return;
    const auto &item = menuConfig.items[itemIndex];
    if (value.empty())
        lastStatusMessage = item.label;
    else
        lastStatusMessage = item.label + ": " + value;

    runtime.scheduler().enqueueOnce([itemIndex, movedValue = std::move(value)] {
        if (!IGI::GAME::GET())
            return;
        runModMenu(menuConfig, runtime, itemIndex, movedValue);
    });
}

#if defined(_WIN32)
extern "C" void __cdecl onDisplayEndFrame() noexcept;

void setKeyboardBlocked(bool blocked) noexcept {
    using KeyboardSetBlockedFn = void(__cdecl *)(char);
    auto *fn = reinterpret_cast<KeyboardSetBlockedFn>(igi::re::HASH::Keyboard_SetBlocked);
    if (fn)
        fn(blocked ? 1 : 0);
}

void restoreIgiRenderCacheAndFpu() noexcept {
    using ResetListsFn = void(__cdecl *)();
    using FpuSetTruncateFn = void(__cdecl *)();
    auto *resetLists = reinterpret_cast<ResetListsFn>(igi::re::HASH::Direct3D_ResetLists);
    if (resetLists)
        resetLists();
    auto *fpuTruncate = reinterpret_cast<FpuSetTruncateFn>(igi::re::HASH::FPU_SetTruncate);
    if (fpuTruncate)
        fpuTruncate();
}

DisplayCallbackNode **endFrameCallbackHead() noexcept {
    return reinterpret_cast<DisplayCallbackNode **>(
        static_cast<std::uintptr_t>(igi::re::HASH::gDisplayEndFrameCallbacks));
}

bool isEndFrameCallbackRegistered() noexcept {
    DisplayCallbackNode *node = *endFrameCallbackHead();
    for (int guard = 0; node && guard < 64; ++guard) {
        if (node == &g_menu.endFrameNode || node->callback == &onDisplayEndFrame)
            return true;
        node = node->next;
    }
    return false;
}

void ensureEndFrameCallbackRegistered() noexcept {
    if (isEndFrameCallbackRegistered())
        return;
    DisplayCallbackNode **head = endFrameCallbackHead();
    g_menu.endFrameNode.callback = &onDisplayEndFrame;
    g_menu.endFrameNode.next = *head;
    *head = &g_menu.endFrameNode;
}

void unregisterEndFrameCallback() noexcept {
    DisplayCallbackNode **head = endFrameCallbackHead();
    DisplayCallbackNode *prev = nullptr;
    DisplayCallbackNode *node = *head;
    for (int guard = 0; node && guard < 64; ++guard) {
        if (node == &g_menu.endFrameNode || node->callback == &onDisplayEndFrame) {
            if (prev)
                prev->next = node->next;
            else
                *head = node->next;
            break;
        }
        prev = node;
        node = node->next;
    }
    g_menu.endFrameNode.next = nullptr;
    g_menu.endFrameNode.callback = nullptr;
}

LRESULT CALLBACK imguiWindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (g_menu.imguiInitialized && menuVisible) {
        if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam))
            return TRUE;

        if (ImGui::GetCurrentContext() != nullptr) {
            const ImGuiIO &io = ImGui::GetIO();
            switch (msg) {
            case WM_LBUTTONDOWN:
            case WM_LBUTTONUP:
            case WM_LBUTTONDBLCLK:
            case WM_RBUTTONDOWN:
            case WM_RBUTTONUP:
            case WM_RBUTTONDBLCLK:
            case WM_MBUTTONDOWN:
            case WM_MBUTTONUP:
            case WM_MBUTTONDBLCLK:
            case WM_MOUSEWHEEL:
            case WM_MOUSEMOVE:
                if (io.WantCaptureMouse)
                    return 0;
                break;
            case WM_KEYDOWN:
            case WM_KEYUP:
            case WM_SYSKEYDOWN:
            case WM_SYSKEYUP:
            case WM_CHAR:
                if (io.WantCaptureKeyboard)
                    return 0;
                break;
            default:
                break;
            }
        }
    }
    if (g_menu.originalWndProc)
        return CallWindowProcA(g_menu.originalWndProc, hwnd, msg, wParam, lParam);
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

void applyImGuiTheme() {
    switch (menuConfig.imgui.theme) {
    case igi::ImGuiTheme::Light:
        ImGui::StyleColorsLight();
        break;
    case igi::ImGuiTheme::Classic:
        ImGui::StyleColorsClassic();
        break;
    case igi::ImGuiTheme::Dark:
    default:
        ImGui::StyleColorsDark();
        break;
    }
    ImGuiIO &io = ImGui::GetIO();
    io.FontGlobalScale = menuConfig.imgui.fontScale;

    ImGuiStyle &style = ImGui::GetStyle();
    style.Alpha = menuConfig.imgui.alpha;
    style.WindowRounding = menuConfig.imgui.windowRounding;
    style.FrameRounding = menuConfig.imgui.frameRounding;
    style.GrabRounding = menuConfig.imgui.frameRounding;
    style.WindowBorderSize = menuConfig.imgui.borderSize;
    style.FrameBorderSize = menuConfig.imgui.borderSize;
    style.ItemSpacing = ImVec2(8.0f, 6.0f);
    style.WindowPadding = ImVec2(12.0f, 12.0f);
}

void shutdownImGuiBackend() noexcept {
    if (!g_menu.imguiInitialized)
        return;

    if (g_menu.boundHwnd && g_menu.originalWndProc) {
        SetWindowLongPtrA(g_menu.boundHwnd, GWLP_WNDPROC,
                          reinterpret_cast<LONG_PTR>(g_menu.originalWndProc));
    }
    g_menu.originalWndProc = nullptr;

    ImGui_ImplDX7_InvalidateDeviceObjects();
    ImGui_ImplDX7_Shutdown();
    ImGui_ImplWin32_Shutdown();
    if (ImGui::GetCurrentContext() != nullptr)
        ImGui::DestroyContext();

    g_menu.boundDevice = nullptr;
    g_menu.boundDDraw = nullptr;
    g_menu.boundHwnd = nullptr;
    g_menu.imguiInitialized = false;
    igi::log(igi::LogLevel::Info, "external imgui backend: Direct3D 7 renderer shut down");
}

bool ensureImGuiBackend() {
    using GetHwndFn = HWND(__cdecl *)();
    auto *getHwnd = reinterpret_cast<GetHwndFn>(igi::re::HASH::AppContext_GethWnd);
    const HWND hwnd = getHwnd ? getHwnd() : nullptr;
    auto *device = *reinterpret_cast<IDirect3DDevice7 *const *>(
        static_cast<std::uintptr_t>(igi::re::HASH::gD3DDevice));
    auto *ddraw = *reinterpret_cast<IDirectDraw7 *const *>(
        static_cast<std::uintptr_t>(igi::re::HASH::gDDraw));

    if (!hwnd || !device || !ddraw)
        return false;

    if (g_menu.imguiInitialized) {
        if (device == g_menu.boundDevice && ddraw == g_menu.boundDDraw && hwnd == g_menu.boundHwnd)
            return true;
        if (hwnd == g_menu.boundHwnd) {
            igi::log(
                igi::LogLevel::Info,
                "external imgui backend: Direct3D 7 device changed; recreating device objects");
            ImGui_ImplDX7_InvalidateDeviceObjects();
            ImGui_ImplDX7_Shutdown();
            if (!ImGui_ImplDX7_Init(device, ddraw) || !ImGui_ImplDX7_CreateDeviceObjects()) {
                igi::log(igi::LogLevel::Error,
                         "external imgui backend: Direct3D 7 device rebind failed");
                shutdownImGuiBackend();
                return false;
            }
            g_menu.boundDevice = device;
            g_menu.boundDDraw = ddraw;
            restoreIgiRenderCacheAndFpu();
            return true;
        }
        igi::log(igi::LogLevel::Info,
                 "external imgui backend: game window changed; reinitializing backend");
        shutdownImGuiBackend();
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.IniFilename = menuConfig.imgui.saveLayout ? menuConfig.imgui.iniFile.c_str() : nullptr;
    if (menuConfig.imgui.keyboardNav)
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    else
        io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableKeyboard;
    applyImGuiTheme();

    if (!ImGui_ImplWin32_Init(hwnd)) {
        ImGui::DestroyContext();
        igi::log(igi::LogLevel::Error, "external imgui backend: ImGui_ImplWin32_Init failed");
        return false;
    }
    if (!ImGui_ImplDX7_Init(device, ddraw)) {
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        igi::log(igi::LogLevel::Error, "external imgui backend: ImGui_ImplDX7_Init failed");
        return false;
    }
    if (!ImGui_ImplDX7_CreateDeviceObjects()) {
        ImGui_ImplDX7_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        igi::log(igi::LogLevel::Error,
                 "external imgui backend: ImGui_ImplDX7_CreateDeviceObjects failed");
        return false;
    }

    g_menu.originalWndProc = reinterpret_cast<WNDPROC>(
        SetWindowLongPtrA(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&imguiWindowProc)));
    g_menu.boundHwnd = hwnd;
    g_menu.boundDevice = device;
    g_menu.boundDDraw = ddraw;
    g_menu.imguiInitialized = true;
    restoreIgiRenderCacheAndFpu();
    igi::log(igi::LogLevel::Info,
             "external imgui backend: initialized Direct3D 7 ImGui renderer on game window");
    return true;
}

bool closeImGuiMenu() {
    menuVisible = false;
    setKeyboardBlocked(false);
    if (IGI::GAME::GET()) {
        if (!igi::resumeFromMenu(runtime)) {
            igi::log(igi::LogLevel::Warning, "external imgui menu close: unable to resume game");
        } else {
            igi::log(igi::LogLevel::Info,
                     "external imgui menu close: game resumed and human input enabled");
        }
    } else {
        igi::log(igi::LogLevel::Info, "external imgui menu close: closed outside active gameplay");
    }
    if (!shutdownPending.load(std::memory_order_acquire))
        safeToShutdown.store(true, std::memory_order_release);
    return true;
}

bool openImGuiMenu() {
    if (!IGI::GAME::GET()) {
        igi::log(igi::LogLevel::Warning,
                 "external imgui menu: IGI::GAME::GET returned null; waiting for active gameplay");
        return false;
    }
    ensureEndFrameCallbackRegistered();
    if (!ensureImGuiBackend()) {
        igi::log(igi::LogLevel::Warning,
                 "external imgui menu: Direct3D 7 device not ready yet; waiting");
        return false;
    }
    menuVisible = true;
    initialAttachPending.store(false, std::memory_order_release);
    safeToShutdown.store(false, std::memory_order_release);
    if (pauseWhileOpen) {
        if (!igi::pauseForMenu(runtime)) {
            igi::log(igi::LogLevel::Warning,
                     "external imgui menu open: pause deferred; locking input only");
            (void)igi::lockHumanInput();
        }
        setKeyboardBlocked(true);
    }
    IGI::MISC::STATUS_MESSAGE_SHOW(nullptr, "IGI SDK ImGui Mod Menu: enabled");
    igi::log(igi::LogLevel::Info,
             "internal menu screen: attached and requested from existing MenuManager");
    igi::log(igi::LogLevel::Info, "external imgui menu: attached and visible");
    return true;
}

void menuFrameWork() {
    if (!runtime.initialized())
        return;
    ensureEndFrameCallbackRegistered();
    if (menuVisible && !IGI::GAME::GET()) {
        (void)closeImGuiMenu();
        return;
    }
    if (!initialAttachPending.load(std::memory_order_acquire) || menuVisible)
        return;
    if (!IGI::GAME::GET() || IGI::GAME::IS_PAUSED())
        return;
    (void)openImGuiMenu();
}

bool toggleImGuiMenuOnGameThread() {
    if (menuVisible)
        return closeImGuiMenu();
    if (!IGI::GAME::GET()) {
        igi::log(igi::LogLevel::Warning, "external imgui menu toggle: requires an active game");
        return false;
    }
    return openImGuiMenu();
}

void renderImGuiModWindow() {
    ImGui::SetNextWindowPos(
        ImVec2(static_cast<float>(menuConfig.imgui.x), static_cast<float>(menuConfig.imgui.y)),
        ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(static_cast<float>(menuConfig.imgui.width),
                                    static_cast<float>(menuConfig.imgui.height)),
                             ImGuiCond_FirstUseEver);

    ImGuiWindowFlags windowFlags = ImGuiWindowFlags_None;
    if (!menuConfig.imgui.collapsible)
        windowFlags |= ImGuiWindowFlags_NoCollapse;
    if (!menuConfig.imgui.movable)
        windowFlags |= ImGuiWindowFlags_NoMove;
    if (!menuConfig.imgui.resizable)
        windowFlags |= ImGuiWindowFlags_NoResize;
    if (menuConfig.imgui.autoResize)
        windowFlags |= ImGuiWindowFlags_AlwaysAutoResize;

    bool keepOpen = menuVisible;
    if (ImGui::Begin(menuConfig.title.c_str(), &keepOpen, windowFlags)) {
        ImGui::Text("IGI Mod Base - ImGui (Direct3D 7)");
        if (menuConfig.imgui.showFps) {
            ImGui::SameLine();
            ImGui::TextDisabled("| %.1f FPS", ImGui::GetIO().Framerate);
        }

        if (ImGui::Checkbox("Pause Game While Open", &pauseWhileOpen)) {
            if (pauseWhileOpen) {
                (void)igi::pauseForMenu(runtime);
                setKeyboardBlocked(true);
            } else {
                setKeyboardBlocked(false);
                (void)igi::resumeFromMenu(runtime);
            }
        }
        ImGui::Separator();

        for (std::size_t index = 0; index < menuConfig.items.size(); ++index) {
            const auto &item = menuConfig.items[index];
            ImGui::PushID(static_cast<int>(index));

            switch (item.kind) {
            case igi::ModMenuItemKind::List: {
                if (index >= listSelections.size())
                    listSelections.resize(menuConfig.items.size(), 0);
                int current = listSelections[index];
                if (current < 0 || static_cast<std::size_t>(current) >= item.values.size()) {
                    current = 0;
                }
                const char *preview = item.values.empty()
                                          ? ""
                                          : item.values[static_cast<std::size_t>(current)].c_str();
                ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.58f);
                if (ImGui::BeginCombo(item.label.c_str(), preview)) {
                    for (std::size_t valIdx = 0; valIdx < item.values.size(); ++valIdx) {
                        const bool isSelected = (static_cast<int>(valIdx) == current);
                        if (ImGui::Selectable(item.values[valIdx].c_str(), isSelected)) {
                            current = static_cast<int>(valIdx);
                            listSelections[index] = current;
                            queueMenuAction(index, item.values[valIdx]);
                        }
                        if (isSelected)
                            ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
                ImGui::SameLine();
                if (ImGui::Button("Apply") && !item.values.empty()) {
                    queueMenuAction(index, item.values[static_cast<std::size_t>(current)]);
                }
                break;
            }
            case igi::ModMenuItemKind::Action: {
                if (ImGui::Button(item.label.c_str(), ImVec2(-1.0f, 0.0f))) {
                    queueMenuAction(index, "");
                }
                break;
            }
            case igi::ModMenuItemKind::Toggle: {
                if (index >= toggleValues.size())
                    toggleValues.resize(menuConfig.items.size(), false);
                bool checked = toggleValues[index];
                if (ImGui::Checkbox(item.label.c_str(), &checked)) {
                    toggleValues[index] = checked;
                    queueMenuAction(index, checked ? "true" : "false");
                }
                break;
            }
            case igi::ModMenuItemKind::Slider: {
                if (index >= sliderValues.size())
                    sliderValues.resize(menuConfig.items.size(), 0);
                int value = sliderValues[index];
                if (ImGui::SliderInt(item.label.c_str(), &value, 0, 100)) {
                    sliderValues[index] = value;
                    queueMenuAction(index, std::to_string(value));
                }
                break;
            }
            }

            ImGui::PopID();
        }

        ImGui::Separator();
        if (menuConfig.imgui.showStatus) {
            ImGui::TextWrapped("Status: %s", lastStatusMessage.c_str());
        }
        if (ImGui::Button("Close Menu", ImVec2(-1.0f, 0.0f))) {
            keepOpen = false;
        }
    }
    ImGui::End();

    if (!keepOpen && menuVisible) {
        runtime.scheduler().enqueueOnce([] { (void)closeImGuiMenu(); });
    }
}

extern "C" void __cdecl onDisplayEndFrame() noexcept {
    if (!runtime.initialized() || !menuVisible)
        return;
    if (!IGI::GAME::GET()) {
        (void)closeImGuiMenu();
        return;
    }
    if (!ensureImGuiBackend())
        return;

    ImGui_ImplWin32_NewFrame();
    ImGui_ImplDX7_NewFrame();

    ImGuiIO &io = ImGui::GetIO();
    D3DVIEWPORT7 vp{};
    if (g_menu.boundDevice && SUCCEEDED(g_menu.boundDevice->GetViewport(&vp)) && vp.dwWidth > 0 &&
        vp.dwHeight > 0) {
        io.DisplaySize = ImVec2(static_cast<float>(vp.dwWidth), static_cast<float>(vp.dwHeight));
    }
    io.MouseDrawCursor = menuVisible && menuConfig.imgui.mouseCursor;
    io.MouseDown[0] = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
    io.MouseDown[1] = (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
    io.MouseDown[2] = (GetAsyncKeyState(VK_MBUTTON) & 0x8000) != 0;

    ImGui::NewFrame();
    renderImGuiModWindow();
    ImGui::EndFrame();
    ImGui::Render();
    ImGui_ImplDX7_RenderDrawData(ImGui::GetDrawData());
    restoreIgiRenderCacheAndFpu();
}
#endif

} // namespace

namespace igi {
bool prepareMenuForLevelChange() {
#if defined(_WIN32)
    if (!runtime.initialized())
        return true;

    initialAttachPending.store(false, std::memory_order_release);
    if (menuVisible)
        (void)closeImGuiMenu();
    else
        (void)resumeFromMenu(runtime);

    log(LogLevel::Info, "level change: closed external ImGui menu and unlocked input");
    return true;
#else
    return false;
#endif
}
} // namespace igi

extern "C" IGI_DEMO_EXPORT bool IGI_MenuManager_External_Initialize() {
    igi::log(igi::LogLevel::Debug, "external imgui initialize: begin");
    menuConfig = igi::loadMenuConfig("project-igi-mod-config.ini");
    if (!configureModMenu(menuConfig))
        return false;
    initializeMenuStateBuffers();

#if defined(_WIN32)
    if (!runtime.initialize(menuConfig.hook)) {
        igi::log(igi::LogLevel::Error, "external imgui initialize: runtime initialization failed");
        return false;
    }
    initialAttachPending.store(true, std::memory_order_release);
    runtime.scheduler().enqueueEveryFrame([] {
        menuFrameWork();
        igi::renderThirdPersonCrosshair();
    });
#else
    runtime.scheduler().bindGameThread();
#endif

    if (!populatePortableMenuModel()) {
        menuModel.reset();
        runtime.shutdown();
        return false;
    }
    igi::log(igi::LogLevel::Info, "external imgui initialize: menu model and D3D7 host staged");
    return true;
}

extern "C" IGI_DEMO_EXPORT std::size_t IGI_MenuManager_External_RunFrame() {
    const auto count = frameCount.fetch_add(1, std::memory_order_relaxed) + 1;
    const auto ran = runtime.scheduler().runPending();
#if defined(_WIN32)
    menuFrameWork();
    igi::renderThirdPersonCrosshair();
#endif
    if (count <= 5 || (count % 1000) == 0)
        igi::log(igi::LogLevel::Debug, "external imgui frame: scheduler completed");
    return ran;
}

extern "C" IGI_DEMO_EXPORT bool IGI_MenuManager_External_Reopen() {
#if defined(_WIN32)
    if (!runtime.initialized()) {
        igi::log(igi::LogLevel::Warning, "external imgui toggle: runtime is not initialized");
        return false;
    }
    runtime.scheduler().enqueueOnce([] { (void)openImGuiMenu(); });
    return true;
#else
    return false;
#endif
}

extern "C" IGI_DEMO_EXPORT bool IGI_MenuManager_External_Toggle() {
#if defined(_WIN32)
    if (!runtime.initialized()) {
        igi::log(igi::LogLevel::Warning, "external imgui toggle: runtime is not initialized");
        return false;
    }
    runtime.scheduler().enqueueOnce([] {
        const bool toggled = toggleImGuiMenuOnGameThread();
        igi::log(toggled ? igi::LogLevel::Info : igi::LogLevel::Error,
                 toggled ? "external imgui toggle: completed on game frame thread"
                         : "external imgui toggle: rejected on game frame thread");
    });
    return true;
#else
    return false;
#endif
}

extern "C" IGI_DEMO_EXPORT bool IGI_MenuManager_External_RequestShutdown() {
#if defined(_WIN32)
    if (!runtime.initialized()) {
        safeToShutdown.store(true, std::memory_order_release);
        return true;
    }
    initialAttachPending.store(false, std::memory_order_release);
    shutdownPending.store(true, std::memory_order_release);
    safeToShutdown.store(false, std::memory_order_release);
    runtime.scheduler().enqueueOnce([] {
        igi::log(igi::LogLevel::Info, "external imgui unload: game-thread cleanup started");
        if (menuVisible)
            (void)closeImGuiMenu();
        else if (IGI::GAME::GET())
            (void)igi::resumeFromMenu(runtime);
        if (IGI::GAME::GET())
            IGI::MISC::STATUS_MESSAGE_SHOW(nullptr, "IGI SDK ImGui Mod Menu: disabled");
        unregisterEndFrameCallback();
        shutdownImGuiBackend();
        igi::log(igi::LogLevel::Info, "external imgui unload: game-thread cleanup finished");
        safeToShutdown.store(true, std::memory_order_release);
    });
    return true;
#else
    return false;
#endif
}

extern "C" IGI_DEMO_EXPORT bool IGI_MenuManager_External_EnsureTick() {
#if defined(_WIN32)
    return runtime.initialized() && runtime.ensureTickRegistered();
#else
    return false;
#endif
}

extern "C" IGI_DEMO_EXPORT std::uint64_t IGI_MenuManager_External_TickCount() {
#if defined(_WIN32)
    return runtime.gameHookInvocationCount();
#else
    return 0;
#endif
}

extern "C" IGI_DEMO_EXPORT bool IGI_MenuManager_External_CanUnload() {
#if defined(_WIN32)
    return safeToShutdown.load(std::memory_order_acquire) && !runtime.scheduler().isRunning();
#else
    return false;
#endif
}

extern "C" IGI_DEMO_EXPORT void IGI_MenuManager_External_Shutdown() {
    igi::log(igi::LogLevel::Debug, "external imgui shutdown: begin");
    if (!safeToShutdown.load(std::memory_order_acquire)) {
        igi::log(igi::LogLevel::Warning,
                 "external imgui shutdown deferred: menu still active on game thread");
        return;
    }
#if defined(_WIN32)
    unregisterEndFrameCallback();
    shutdownImGuiBackend();
#endif
    if (menuModel) {
        menuModel->shutdown();
        menuModel.reset();
    }
    runtime.shutdown();
    listSelections.clear();
    toggleValues.clear();
    sliderValues.clear();
    lastStatusMessage.clear();
    menuVisible = false;
    initialAttachPending.store(false, std::memory_order_release);
    shutdownPending.store(false, std::memory_order_release);
    safeToShutdown.store(true, std::memory_order_release);
    igi::log(igi::LogLevel::Info, "external imgui shutdown: complete");
}

// Keep the shared internal symbol entry points forwarded so the common harness
// and loader tools can drive both internal and external DLL targets identically.
extern "C" IGI_DEMO_EXPORT bool IGI_MenuManager_Internal_Initialize() {
    return IGI_MenuManager_External_Initialize();
}

extern "C" IGI_DEMO_EXPORT std::size_t IGI_MenuManager_Internal_RunFrame() {
    return IGI_MenuManager_External_RunFrame();
}

extern "C" IGI_DEMO_EXPORT bool IGI_MenuManager_Internal_Reopen() {
    return IGI_MenuManager_External_Reopen();
}

extern "C" IGI_DEMO_EXPORT bool IGI_MenuManager_Internal_Toggle() {
    return IGI_MenuManager_External_Toggle();
}

extern "C" IGI_DEMO_EXPORT bool IGI_MenuManager_Internal_RequestShutdown() {
    return IGI_MenuManager_External_RequestShutdown();
}

extern "C" IGI_DEMO_EXPORT bool IGI_MenuManager_Internal_EnsureTick() {
    return IGI_MenuManager_External_EnsureTick();
}

extern "C" IGI_DEMO_EXPORT std::uint64_t IGI_MenuManager_Internal_TickCount() {
    return IGI_MenuManager_External_TickCount();
}

extern "C" IGI_DEMO_EXPORT bool IGI_MenuManager_Internal_CanUnload() {
    return IGI_MenuManager_External_CanUnload();
}

extern "C" IGI_DEMO_EXPORT void IGI_MenuManager_Internal_Shutdown() {
    IGI_MenuManager_External_Shutdown();
}

namespace igi {
const DllMenuCallbacks &getDllMenuCallbacks() noexcept {
    static const DllMenuCallbacks callbacks{
        &IGI_MenuManager_External_Initialize,      &IGI_MenuManager_External_Toggle,
        &IGI_MenuManager_External_RequestShutdown, &IGI_MenuManager_External_CanUnload,
        &IGI_MenuManager_External_Shutdown,        &IGI_MenuManager_External_EnsureTick,
        &IGI_MenuManager_External_TickCount,
    };
    return callbacks;
}
} // namespace igi
