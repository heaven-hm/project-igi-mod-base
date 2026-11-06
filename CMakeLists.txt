cmake_minimum_required(VERSION 3.20)
project(project_igi_natives_sdk VERSION 0.1.0 LANGUAGES CXX)

# Retail IGI 1 is a PE32 x86 process. A 64-bit DLL cannot be loaded into it,
# so reject that misleading configuration at generation time.
if(WIN32 AND NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "IGI 1 SDK Windows targets require a 32-bit x86 toolchain")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Python3 COMPONENTS Interpreter REQUIRED)

add_library(igi_qsc_compiler STATIC
    third_party/igi1conv/qsc_lexer.cpp
    third_party/igi1conv/qsc_parser.cpp
    third_party/igi1conv/qvm_compiler.cpp
)
set_target_properties(igi_qsc_compiler PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(igi_qsc_compiler PUBLIC third_party/igi1conv)

# IGI-RE native bindings: regenerates include/igi_sdk/natives/igi_re/hashes.hpp
# from data/IGI1/neonix-igi-re-names.tsv whenever the symbol table changes.
set(IGI_RE_HASH_HEADER ${CMAKE_CURRENT_SOURCE_DIR}/include/igi_sdk/natives/igi_re/hashes.hpp)
add_custom_command(
    OUTPUT ${IGI_RE_HASH_HEADER}
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_igi_hash.py
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_igi_hash.py
            ${CMAKE_CURRENT_SOURCE_DIR}/data/IGI1/neonix-igi-re-names.tsv
    COMMENT "Generating IGI-RE HASH bindings from names.tsv"
    VERBATIM
)
add_custom_target(igi_re_hash_bindings DEPENDS ${IGI_RE_HASH_HEADER})

add_library(igi_sdk STATIC
    src/igi_sdk/config/menu_config.cpp
    src/igi_sdk/logging/logging.cpp
    src/igi_sdk/ui/menu_manager.cpp
    src/igi_sdk/ui/igi1_menu_manager.cpp
    src/igi_sdk/ui/menu_script.cpp
    src/igi_sdk/ui/menu_resource.cpp
    src/igi_sdk/ui/menu_resource_igi1.cpp
    src/igi_sdk/ui/menu_script_bridge.cpp
    src/igi_sdk/ui/menu_script_igi1.cpp
    src/igi_sdk/ui/menu_values.cpp
    src/igi_sdk/natives/native_catalogue.cpp
    src/igi_sdk/natives/native_caller.cpp
    src/igi_sdk/natives/mvp_natives.cpp
    src/igi_sdk/natives/igi1_native_registry.cpp
    src/igi_sdk/natives/domains/flow/flow.cpp
    src/igi_sdk/natives/native_runner.cpp
    src/igi_sdk/runtime/scheduler.cpp
    src/igi_sdk/runtime/target_profile.cpp
    src/igi_sdk/runtime/runtime.cpp
    src/igi_sdk/runtime/hook_manager.cpp
    src/igi_sdk/runtime/macos_hook_backend.cpp
)
target_include_directories(igi_sdk PUBLIC include)
target_link_libraries(igi_sdk PRIVATE igi_qsc_compiler)
add_dependencies(igi_sdk igi_re_hash_bindings)
target_compile_definitions(igi_sdk PUBLIC IGI_SDK_IGI1_ONLY=1)

add_executable(igi_menu_script tools/generate_menu_script.cpp)
target_link_libraries(igi_menu_script PRIVATE igi_sdk)

option(IGI_SDK_BUILD_DEMOS "Build the internal and external-style demo targets" ON)

if(IGI_SDK_BUILD_DEMOS)
    set(IGI_INTERNAL_MENU_PLATFORM demos/internal-menumanager/platform_linux.cpp)
    if(APPLE)
        set(IGI_INTERNAL_MENU_PLATFORM demos/internal-menumanager/platform_macos.cpp)
    elseif(WIN32)
        set(IGI_INTERNAL_MENU_PLATFORM demos/internal-menumanager/platform_windows.cpp)
    endif()
    add_library(igi_menu_manager_internal SHARED
        demos/internal-menumanager/internal_demo.cpp
        ${IGI_INTERNAL_MENU_PLATFORM}
    )
    target_link_libraries(igi_menu_manager_internal PRIVATE igi_sdk)
    target_include_directories(igi_menu_manager_internal PRIVATE include)
    set_target_properties(igi_menu_manager_internal PROPERTIES OUTPUT_NAME "IGI-MenuManager-Internal" PREFIX "")

    add_executable(igi_menu_manager_external demos/imgui/external_demo.cpp)
    target_link_libraries(igi_menu_manager_external PRIVATE igi_sdk)
    target_include_directories(igi_menu_manager_external PRIVATE include)
    target_compile_definitions(igi_menu_manager_external PRIVATE IGI_SDK_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    set_target_properties(igi_menu_manager_external PROPERTIES OUTPUT_NAME "IGI-MenuManager-External")
endif()

if(WIN32)
    target_compile_definitions(igi_sdk PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_link_libraries(igi_sdk PRIVATE advapi32)
    add_library(igi_sdk_igi1 SHARED src/igi_sdk/runtime/dll_exports.cpp)
    target_link_libraries(igi_sdk_igi1 PRIVATE igi_sdk)
    target_compile_definitions(igi_sdk_igi1 PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    set_target_properties(igi_sdk_igi1 PROPERTIES OUTPUT_NAME "IGI-Natives-SDK-IGI1" PREFIX ""
        WINDOWS_EXPORT_ALL_SYMBOLS ON)
    add_executable(igi_sdk_win32_loader tools/win32_loader.cpp)
    set_target_properties(igi_sdk_win32_loader PROPERTIES OUTPUT_NAME "IGI-SDK-Loader")

    add_executable(igi1_injector tools/igi1_injector.cpp)
    set_target_properties(igi1_injector PROPERTIES OUTPUT_NAME "IGI1-Injector")
target_link_libraries(igi1_injector PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lpthread -Wl,-Bdynamic)
else()
    add_library(igi_sdk_igi1 SHARED src/igi_sdk/runtime/portable_exports.cpp)
    target_link_libraries(igi_sdk_igi1 PRIVATE igi_sdk)
    set_target_properties(igi_sdk_igi1 PROPERTIES OUTPUT_NAME "IGI-Natives-SDK-IGI1")
endif()

add_executable(igi_sdk_tests
    tests/SchedulerTests.cpp
    tests/MenuManagerTests.cpp
)
target_link_libraries(igi_sdk_tests PRIVATE igi_sdk)
target_compile_definitions(igi_sdk_tests PRIVATE IGI_SDK_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")

add_custom_command(
    OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/tests/generated_demo_natives.cpp
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_demo_native_tests.py
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_demo_native_tests.py
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/demo-natives.tsv
            ${CMAKE_CURRENT_SOURCE_DIR}/data/IGI1/mvp-300.tsv
            ${CMAKE_CURRENT_SOURCE_DIR}/include/igi_sdk/IGI.hpp
)
add_executable(igi_demo_native_contract_tests tests/generated_demo_natives.cpp)
target_link_libraries(igi_demo_native_contract_tests PRIVATE igi_sdk)
target_compile_definitions(igi_demo_native_contract_tests PRIVATE IGI_SDK_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")

enable_testing()
add_test(NAME igi_sdk_tests COMMAND igi_sdk_tests)
add_test(NAME igi_generated_natives
         COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_generated_natives.py)
add_test(NAME igi_demo_native_contract
         COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_demo_native_contract.py)
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
add_test(NAME igi_demo_natives_unicorn
         COMMAND ${IGI_UNICORN_PYTHON} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_demo_natives_unicorn.py)
set_tests_properties(igi_demo_natives_unicorn PROPERTIES
    SKIP_RETURN_CODE 77
    ENVIRONMENT "IGI_EXE=${IGI_TEST_EXE}")

if(WIN32)
    add_library(igi_sdk_native_abi_fixture SHARED tests/native_abi_fixture.cpp)
    target_link_libraries(igi_sdk_native_abi_fixture PRIVATE igi_sdk)
    set_target_properties(igi_sdk_native_abi_fixture PROPERTIES
        OUTPUT_NAME "igi-sdk-native-abi-fixture" PREFIX "")
    add_test(NAME igi_sdk_native_wrappers_unicorn
             COMMAND ${Python3_EXECUTABLE}
                     ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_sdk_native_wrappers_unicorn.py
                     $<TARGET_FILE:igi_sdk_native_abi_fixture>)
    set_tests_properties(igi_sdk_native_wrappers_unicorn PROPERTIES SKIP_RETURN_CODE 77)
endif()
