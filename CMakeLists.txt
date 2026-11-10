cmake_minimum_required(VERSION 3.20)
project(project_igi_mod_base VERSION 0.2.0 LANGUAGES C CXX)

# Project I.G.I. (igi.exe) is strictly a 32-bit (x86) Windows PE executable.
# 64-bit / x64 builds are strictly forbidden across all Windows targets.
if(WIN32)
    if(CMAKE_SIZEOF_VOID_P EQUAL 8 OR CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|x64|arm64|aarch64")
        message(FATAL_ERROR "64-bit (x64) Windows builds are strictly forbidden! Project I.G.I. (igi.exe) is a 32-bit (x86) executable only. Target 32-bit x86 (e.g. cmake -A Win32 or i686-w64-mingw32).")
    endif()
    if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
        message(FATAL_ERROR "Project I.G.I. requires a 32-bit (x86) toolchain (CMAKE_SIZEOF_VOID_P must be 4). 64-bit builds are strictly forbidden.")
    endif()
endif()

# Retail IGI 1 igi.exe is a 2002 ANSI Win32 process. The SDK DLLs must load
# inside it through a remote LoadLibrary call, so they must not depend on the
# mingw runtime DLLs (libstdc++-6/libgcc_s/libwinpthread). Homebrew's mingw-w64
# ignores -mcrtdll=msvcrt and always emits the UCRT api-ms-win-crt-* imports,
# which Wine 11 resolves through ucrtbase.dll, but the shared mingw runtime does
# not resolve reliably in the game process. Every Windows target therefore links
# with -static so the C++ runtime is embedded and the only imports left are the
# system DLLs and the UCRT API sets the loader provides.
if(WIN32)
    add_link_options(-mcrtdll=msvcrt)
    if(MINGW)
        add_link_options($<$<CONFIG:Release>:-Wl,--strip-debug>)
    endif()
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Python3 COMPONENTS Interpreter REQUIRED)

find_program(CLANG_FORMAT_EXECUTABLE clang-format)
if(CLANG_FORMAT_EXECUTABLE)
    add_custom_target(format_cpp
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/format_cpp.py
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Formatting first-party C++ sources")
    add_custom_target(format_cpp_check
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/format_cpp.py --check
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Checking first-party C++ formatting")
endif()

find_program(CLANG_TIDY_EXECUTABLE clang-tidy HINTS "$ENV{LLVM_HOME}/bin" "/opt/homebrew/opt/llvm/bin")
if(CLANG_TIDY_EXECUTABLE)
    add_custom_target(lint_cpp
        COMMAND ${CMAKE_COMMAND} -E env CLANG_TIDY=${CLANG_TIDY_EXECUTABLE}
                ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/lint_cpp.py --build-dir ${CMAKE_BINARY_DIR}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Linting first-party C++ translation units")
endif()

add_library(igi_qsc_compiler STATIC
    third_party/igi1conv/qsc_lexer.cpp
    third_party/igi1conv/qsc_parser.cpp
    third_party/igi1conv/qvm_compiler.cpp
)
set_target_properties(igi_qsc_compiler PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(igi_qsc_compiler PUBLIC third_party/igi1conv)

# IGI-RE native bindings: regenerates src/natives/igi_hashes/hashes.hpp
# from the local native catalogue whenever the symbol table changes.
set(IGI_RE_HASH_HEADER ${CMAKE_CURRENT_SOURCE_DIR}/src/natives/igi_hashes/hashes.hpp)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/data/igi-natives.csv")
add_custom_command(
    OUTPUT ${IGI_RE_HASH_HEADER}
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_igi_hash.py
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_igi_hash.py
            ${CMAKE_CURRENT_SOURCE_DIR}/data/igi-natives.csv
    COMMENT "Generating IGI-RE HASH bindings from names.tsv"
    VERBATIM
)
add_custom_target(igi_re_hash_bindings DEPENDS ${IGI_RE_HASH_HEADER})
else()
    message(STATUS "Local IGI data absent; using checked-in native bindings")
    add_custom_target(igi_re_hash_bindings)
endif()

add_library(mod_base STATIC
    src/config/menu_config.cpp
    src/logging/logging.cpp
    src/ui/menu_manager.cpp
    src/ui/igi_menu_manager.cpp
    src/ui/menu_script.cpp
    src/ui/menu_commands.cpp
    src/ui/menu_resource.cpp
    src/ui/menu_resource_igi.cpp
    src/ui/menu_script_bridge.cpp
    src/ui/menu_script_igi.cpp
    src/ui/menu_values.cpp
    src/ui/menu_lifecycle.cpp
    src/natives/native_catalogue.cpp
    src/natives/native_caller.cpp
    src/natives/mvp_natives.cpp
    src/natives/igi_native_registry.cpp
    src/natives/domains/flow/flow.cpp
    src/natives/native_runner.cpp
    src/runtime/scheduler.cpp
    src/runtime/target_profile.cpp
    src/runtime/runtime.cpp
    src/runtime/hook_manager.cpp
    src/runtime/macos_hook_backend.cpp
)
set_target_properties(mod_base PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(mod_base PUBLIC src)
target_link_libraries(mod_base PRIVATE igi_qsc_compiler)
add_dependencies(mod_base igi_re_hash_bindings)
target_compile_definitions(mod_base PUBLIC IGI_SDK_IGI_ONLY=1)

add_executable(igi_menu_script tools/generate_menu_script.cpp)
target_link_libraries(igi_menu_script PRIVATE mod_base)

option(IGI_SDK_BUILD_DEMOS "Build the internal and external-style demo targets" ON)

if(WIN32)
    add_library(igi_imgui_dx7 STATIC
        third_party/imgui/imgui.cpp
        third_party/imgui/imgui_draw.cpp
        third_party/imgui/imgui_tables.cpp
        third_party/imgui/imgui_widgets.cpp
        third_party/imgui/imgui_impl_win32.cpp
        third_party/imgui/imgui_impl_dx7.cpp
    )
    set_target_properties(igi_imgui_dx7 PROPERTIES POSITION_INDEPENDENT_CODE ON)
    target_include_directories(igi_imgui_dx7 PUBLIC third_party/imgui)
    target_compile_definitions(igi_imgui_dx7 PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_link_libraries(igi_imgui_dx7 PUBLIC ddraw dxguid gdi32 imm32 dwmapi)
endif()

if(IGI_SDK_BUILD_DEMOS)
    set(IGI_INTERNAL_MENU_PLATFORM)
    if(APPLE)
        set(IGI_INTERNAL_MENU_PLATFORM demos/support/platform_macos.cpp)
    elseif(WIN32)
        set(IGI_INTERNAL_MENU_PLATFORM src/platform/windows_dll.cpp)
    endif()
    add_library(igi_basic_mod_menu SHARED
        src/mod_base/menu_host.cpp
        demos/internal/basic-mod/basic_menu.cpp
        demos/helpers/menu_helpers.cpp
        ${IGI_INTERNAL_MENU_PLATFORM}
    )
    target_link_libraries(igi_basic_mod_menu PRIVATE mod_base)
    target_include_directories(igi_basic_mod_menu PRIVATE src)
    if(WIN32)
        target_link_options(igi_basic_mod_menu PRIVATE -static -Wl,--image-base,0x64000000)
    endif()
    set_target_properties(igi_basic_mod_menu PROPERTIES OUTPUT_NAME "igi-basic-mod-internal" PREFIX "")

    add_library(igi_basic_mod_menu_external SHARED
        src/mod_base/imgui_menu_host.cpp
        demos/external/basic-mod/basic_menu.cpp
        demos/helpers/menu_helpers.cpp
        ${IGI_INTERNAL_MENU_PLATFORM}
    )
    target_link_libraries(igi_basic_mod_menu_external PRIVATE mod_base)
    target_include_directories(igi_basic_mod_menu_external PRIVATE src)
    if(WIN32)
        target_link_libraries(igi_basic_mod_menu_external PRIVATE igi_imgui_dx7)
        target_link_options(igi_basic_mod_menu_external PRIVATE -static -Wl,--image-base,0x64000000)
    endif()
    set_target_properties(igi_basic_mod_menu_external PROPERTIES OUTPUT_NAME "igi-basic-mod-external" PREFIX "")

    add_library(igi_simple_mod_menu_external SHARED
        src/mod_base/imgui_menu_host.cpp
        demos/external/simple-mod/simple_menu.cpp
        demos/helpers/menu_helpers.cpp
        ${IGI_INTERNAL_MENU_PLATFORM}
    )
    target_link_libraries(igi_simple_mod_menu_external PRIVATE mod_base)
    target_include_directories(igi_simple_mod_menu_external PRIVATE src)
    if(WIN32)
        target_link_libraries(igi_simple_mod_menu_external PRIVATE igi_imgui_dx7)
        target_link_options(igi_simple_mod_menu_external PRIVATE -static -Wl,--image-base,0x64000000)
    endif()
    set_target_properties(igi_simple_mod_menu_external PROPERTIES OUTPUT_NAME "igi-simple-mod-external" PREFIX "")

    add_library(igi_advanced_mod_menu_external SHARED
        src/mod_base/imgui_menu_host.cpp
        demos/external/advanced-mod/advanced_menu.cpp
        demos/external/simple-mod/simple_menu.cpp
        demos/helpers/menu_helpers.cpp
        ${IGI_INTERNAL_MENU_PLATFORM}
    )
    target_link_libraries(igi_advanced_mod_menu_external PRIVATE mod_base)
    target_include_directories(igi_advanced_mod_menu_external PRIVATE src)
    target_compile_definitions(igi_advanced_mod_menu_external PRIVATE IGI_ADVANCED_MENU=1)
    if(WIN32)
        target_link_libraries(igi_advanced_mod_menu_external PRIVATE igi_imgui_dx7)
        target_link_options(igi_advanced_mod_menu_external PRIVATE -static -Wl,--image-base,0x64000000)
    endif()
    set_target_properties(igi_advanced_mod_menu_external PROPERTIES OUTPUT_NAME "igi-advanced-mod-external" PREFIX "")

    add_library(igi_heaven_mod_menu_external SHARED src/mod_base/imgui_menu_host.cpp demos/heaven/heaven_menu.cpp demos/helpers/menu_helpers.cpp ${IGI_INTERNAL_MENU_PLATFORM})
    target_link_libraries(igi_heaven_mod_menu_external PRIVATE mod_base)
    target_include_directories(igi_heaven_mod_menu_external PRIVATE src demos)
    if(WIN32)
        target_link_libraries(igi_heaven_mod_menu_external PRIVATE igi_imgui_dx7)
        target_link_options(igi_heaven_mod_menu_external PRIVATE -static -Wl,--image-base,0x64000000)
    endif()
    set_target_properties(igi_heaven_mod_menu_external PROPERTIES OUTPUT_NAME "igi-heaven-mod-external" PREFIX "")

    add_library(igi_heaven_mod_advanced_menu_external SHARED src/mod_base/imgui_menu_host.cpp demos/heaven/heaven_menu.cpp demos/helpers/menu_helpers.cpp ${IGI_INTERNAL_MENU_PLATFORM})
    target_link_libraries(igi_heaven_mod_advanced_menu_external PRIVATE mod_base)
    target_include_directories(igi_heaven_mod_advanced_menu_external PRIVATE src demos)
    target_compile_definitions(igi_heaven_mod_advanced_menu_external PRIVATE IGI_HEAVEN_ADVANCED=1)
    if(WIN32)
        target_link_libraries(igi_heaven_mod_advanced_menu_external PRIVATE igi_imgui_dx7)
        target_link_options(igi_heaven_mod_advanced_menu_external PRIVATE -static -Wl,--image-base,0x64000000)
    endif()
    set_target_properties(igi_heaven_mod_advanced_menu_external PROPERTIES OUTPUT_NAME "igi-heaven-mod-advanced-external" PREFIX "")

    if(WIN32)
        add_library(igi_simple_mod_menu SHARED demos/internal/simple-mod/simple_menu.cpp demos/helpers/menu_helpers.cpp src/mod_base/menu_host.cpp ${IGI_INTERNAL_MENU_PLATFORM})
        target_link_libraries(igi_simple_mod_menu PRIVATE mod_base)
        target_link_options(igi_simple_mod_menu PRIVATE -static)
        set_target_properties(igi_simple_mod_menu PROPERTIES OUTPUT_NAME "igi-simple-mod-internal" PREFIX "")
        add_library(igi_advanced_mod_menu SHARED demos/internal/advanced-mod/advanced_menu.cpp demos/internal/simple-mod/simple_menu.cpp demos/helpers/menu_helpers.cpp src/mod_base/menu_host.cpp ${IGI_INTERNAL_MENU_PLATFORM})
        target_link_libraries(igi_advanced_mod_menu PRIVATE mod_base)
        target_compile_definitions(igi_advanced_mod_menu PRIVATE IGI_ADVANCED_MENU=1)
        target_link_options(igi_advanced_mod_menu PRIVATE -static)
        set_target_properties(igi_advanced_mod_menu PROPERTIES OUTPUT_NAME "igi-advanced-mod-internal" PREFIX "")
        add_library(igi_heaven_mod_menu SHARED demos/heaven/heaven_menu.cpp demos/helpers/menu_helpers.cpp src/mod_base/menu_host.cpp ${IGI_INTERNAL_MENU_PLATFORM})
        target_link_libraries(igi_heaven_mod_menu PRIVATE mod_base)
        target_include_directories(igi_heaven_mod_menu PRIVATE demos)
        target_link_options(igi_heaven_mod_menu PRIVATE -static)
        set_target_properties(igi_heaven_mod_menu PROPERTIES OUTPUT_NAME "igi-heaven-mod-internal" PREFIX "")
        add_library(igi_heaven_mod_advanced_menu SHARED demos/heaven/heaven_menu.cpp demos/helpers/menu_helpers.cpp src/mod_base/menu_host.cpp ${IGI_INTERNAL_MENU_PLATFORM})
        target_link_libraries(igi_heaven_mod_advanced_menu PRIVATE mod_base)
        target_include_directories(igi_heaven_mod_advanced_menu PRIVATE demos)
        target_compile_definitions(igi_heaven_mod_advanced_menu PRIVATE IGI_HEAVEN_ADVANCED=1)
        target_link_options(igi_heaven_mod_advanced_menu PRIVATE -static)
        set_target_properties(igi_heaven_mod_advanced_menu PROPERTIES OUTPUT_NAME "igi-heaven-mod-advanced-internal" PREFIX "")
        if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/demos/status-message/status_message_demo.cpp")
        add_library(igi_natives_inject_test SHARED demos/status-message/status_message_demo.cpp)
        target_link_libraries(igi_natives_inject_test PRIVATE mod_base)
        target_include_directories(igi_natives_inject_test PRIVATE src)
        target_compile_definitions(igi_natives_inject_test PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
        target_link_options(igi_natives_inject_test PRIVATE -static)
        set_target_properties(igi_natives_inject_test PROPERTIES OUTPUT_NAME "igi-natives-inject-test" PREFIX "")
        endif()
    endif()

    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/demos/imgui/external_demo.cpp")
    add_executable(igi_menu_manager_external demos/imgui/external_demo.cpp)
    target_link_libraries(igi_menu_manager_external PRIVATE mod_base)
    target_include_directories(igi_menu_manager_external PRIVATE src)
    target_compile_definitions(igi_menu_manager_external PRIVATE IGI_SDK_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    set_target_properties(igi_menu_manager_external PROPERTIES OUTPUT_NAME "IGI-MenuManager-External")
    endif()
endif()

if(WIN32)
    target_compile_definitions(mod_base PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    add_library(igi_minhook STATIC
        third_party/minhook/src/buffer.c
        third_party/minhook/src/hook.c
        third_party/minhook/src/trampoline.c
        third_party/minhook/src/hde/hde32.c
    )
    target_include_directories(igi_minhook PUBLIC third_party/minhook/include)
    target_compile_definitions(igi_minhook PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_link_libraries(mod_base PRIVATE igi_minhook)
    target_link_libraries(mod_base PRIVATE advapi32)
    add_library(igi_sdk_igi SHARED src/runtime/dll_exports.cpp)
    target_link_libraries(igi_sdk_igi PRIVATE mod_base)
    target_compile_definitions(igi_sdk_igi PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    # Embed the mingw C++ runtime so the injected DLL is self-contained: the
    # game process must not need libstdc++-6/libgcc_s/libwinpthread present.
    target_link_options(igi_sdk_igi PRIVATE -static)
    set_target_properties(igi_sdk_igi PROPERTIES OUTPUT_NAME "IGI-Natives-SDK-IGI" PREFIX ""
        WINDOWS_EXPORT_ALL_SYMBOLS ON)
    add_executable(igi_sdk_win32_loader tools/win32_loader.cpp)
    set_target_properties(igi_sdk_win32_loader PROPERTIES OUTPUT_NAME "IGI-SDK-Loader")

    add_executable(igi_injector tools/igi_injector.cpp)
    set_target_properties(igi_injector PROPERTIES OUTPUT_NAME "IGI-Injector")
target_link_libraries(igi_injector PRIVATE -static)

    # Small Windows-side input driver used by the Wine live-game validation.
    # Sending keys from inside the Wine desktop avoids relying on macOS
    # Accessibility permission or on the host application's focus state.
    add_executable(igi_keypress tools/igi_keypress.cpp)
    target_link_options(igi_keypress PRIVATE -static)
    set_target_properties(igi_keypress PROPERTIES OUTPUT_NAME "IGI-Keypress")

else()
    add_library(igi_sdk_igi SHARED src/runtime/portable_exports.cpp)
    target_link_libraries(igi_sdk_igi PRIVATE mod_base)
    set_target_properties(igi_sdk_igi PROPERTIES OUTPUT_NAME "IGI-Natives-SDK-IGI")
endif()

add_executable(igi_sdk_tests
    tests/SchedulerTests.cpp
    tests/MenuManagerTests.cpp
)
target_link_libraries(igi_sdk_tests PRIVATE mod_base)
if(WIN32)
    target_link_options(igi_sdk_tests PRIVATE -static)
endif()
target_compile_definitions(igi_sdk_tests PRIVATE IGI_SDK_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/data/igi-natives.csv")
add_custom_command(
    OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/tests/generated_demo_natives.cpp
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_demo_native_tests.py
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_demo_native_tests.py
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/demo-natives.tsv
            ${CMAKE_CURRENT_SOURCE_DIR}/data/igi-natives.csv
            ${CMAKE_CURRENT_SOURCE_DIR}/src/natives/mvp_natives.hpp
)
endif()
add_executable(igi_demo_native_contract_tests tests/generated_demo_natives.cpp)
target_link_libraries(igi_demo_native_contract_tests PRIVATE mod_base)
target_compile_definitions(igi_demo_native_contract_tests PRIVATE IGI_SDK_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")

enable_testing()
# The shipping Heaven control set must stay large enough to be useful and must
# always generate a complete retail menu resource (command list past frame size).
add_executable(igi_heaven_layout_tests
    tests/HeavenLayoutTests.cpp
    demos/heaven/heaven_menu.cpp
    demos/helpers/menu_helpers.cpp
)
target_link_libraries(igi_heaven_layout_tests PRIVATE mod_base)
target_include_directories(igi_heaven_layout_tests PRIVATE src demos)
if(WIN32)
    target_link_options(igi_heaven_layout_tests PRIVATE -static)
endif()

add_executable(igi_simple_layout_tests
    tests/SimpleMenuLayoutTests.cpp
    demos/internal/simple-mod/simple_menu.cpp
    demos/helpers/menu_helpers.cpp
)
target_link_libraries(igi_simple_layout_tests PRIVATE mod_base)
target_include_directories(igi_simple_layout_tests PRIVATE src demos)
add_test(NAME igi_simple_layout COMMAND igi_simple_layout_tests)
add_test(NAME igi_sdk_tests COMMAND igi_sdk_tests)
add_test(NAME igi_heaven_layout COMMAND igi_heaven_layout_tests)
add_test(NAME igi_heaven_menu_contract
         COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_heaven_menu_contract.py)
add_test(NAME igi_native_signatures
         COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_native_signatures.py)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/data/igi-natives.csv")
add_test(NAME igi_generated_natives
         COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_generated_natives.py)
add_test(NAME igi_demo_native_contract
         COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_demo_native_contract.py)
add_test(NAME igi_imgui_dx7_state_backup
         COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_imgui_dx7_state_backup.py)
add_test(NAME igi_hook_manager_threading
         COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_hook_manager_threading.py)
endif()
# The Unicorn ABI probe loads the retail 32-bit IGI 1 PE. On Apple Silicon the
# script must run under x86_64 Python via Rosetta, so wrap the interpreter when
# the host is arm64 macOS. IGI_TEST_EXE points at the supported retail
# executable; when it is missing the script reports SKIP (exit code 77).
set(IGI_TEST_EXE "$ENV{HOME}/Downloads/IGI/igi.exe" CACHE FILEPATH
    "Retail 32-bit IGI 1 executable for the Unicorn ABI probe")
# Homebrew's Python is arm64-only and cannot execute under Rosetta, so on
# Apple Silicon use the universal system interpreter for the 32-bit probe.
set(IGI_UNICORN_PYTHON ${Python3_EXECUTABLE})
if(APPLE AND CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64")
    if(EXISTS "/usr/bin/python3")
        set(IGI_UNICORN_PYTHON arch -x86_64 /usr/bin/python3)
    else()
        set(IGI_UNICORN_PYTHON arch -x86_64 ${Python3_EXECUTABLE})
    endif()
endif()
add_test(NAME igi_menu_helpers_unicorn
         COMMAND ${IGI_UNICORN_PYTHON} ${CMAKE_CURRENT_SOURCE_DIR}/tools/igi_runtime_harness.py
                 --verify-menu-helpers --exe ${IGI_TEST_EXE} --dll "")
set_tests_properties(igi_menu_helpers_unicorn PROPERTIES
    SKIP_RETURN_CODE 77
    ENVIRONMENT "IGI_EXE=${IGI_TEST_EXE}")
add_test(NAME igi_heaven_flows_unicorn
         COMMAND ${IGI_UNICORN_PYTHON} ${CMAKE_CURRENT_SOURCE_DIR}/tools/igi_runtime_harness.py
                 --verify-heaven-flows --exe ${IGI_TEST_EXE})
set_tests_properties(igi_heaven_flows_unicorn PROPERTIES
    SKIP_RETURN_CODE 77
    ENVIRONMENT "IGI_EXE=${IGI_TEST_EXE}")
add_test(NAME igi_demo_natives_unicorn
         COMMAND ${IGI_UNICORN_PYTHON} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_demo_natives_unicorn.py)
set_tests_properties(igi_demo_natives_unicorn PROPERTIES
    SKIP_RETURN_CODE 77
    ENVIRONMENT "IGI_EXE=${IGI_TEST_EXE}")

if(WIN32)
    add_library(igi_sdk_native_abi_fixture SHARED tests/native_abi_fixture.cpp)
    target_link_libraries(igi_sdk_native_abi_fixture PRIVATE mod_base)
    target_link_options(igi_sdk_native_abi_fixture PRIVATE -static)
    set_target_properties(igi_sdk_native_abi_fixture PROPERTIES
        OUTPUT_NAME "igi-sdk-native-abi-fixture" PREFIX "")
    add_test(NAME igi_sdk_native_wrappers_unicorn
             COMMAND ${Python3_EXECUTABLE}
                     ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_sdk_native_wrappers_unicorn.py
                     $<TARGET_FILE:igi_sdk_native_abi_fixture>)
    set_tests_properties(igi_sdk_native_wrappers_unicorn PROPERTIES SKIP_RETURN_CODE 77)
endif()
