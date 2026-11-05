cmake_minimum_required(VERSION 3.20)
project(project_igi_natives_sdk VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(igi_sdk STATIC
    src/igi_sdk/logging/logging.cpp
    src/igi_sdk/ui/menu_manager.cpp
    src/igi_sdk/natives/native_catalogue.cpp
    src/igi_sdk/runtime/scheduler.cpp
    src/igi_sdk/runtime/target_profile.cpp
    src/igi_sdk/runtime/runtime.cpp
    src/igi_sdk/runtime/hook_manager.cpp
)
target_include_directories(igi_sdk PUBLIC include)
target_compile_definitions(igi_sdk PUBLIC IGI_SDK_IGI1_ONLY=1)

if(WIN32)
    target_compile_definitions(igi_sdk PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_link_libraries(igi_sdk PRIVATE advapi32)
    add_library(igi_sdk_igi1 SHARED src/igi_sdk/runtime/dll_exports.cpp)
    target_link_libraries(igi_sdk_igi1 PRIVATE igi_sdk)
    target_compile_definitions(igi_sdk_igi1 PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    set_target_properties(igi_sdk_igi1 PROPERTIES OUTPUT_NAME "IGI-Natives-SDK-IGI1" PREFIX "")
endif()

add_executable(igi_sdk_tests
    tests/SchedulerTests.cpp
    tests/MenuManagerTests.cpp
)
target_link_libraries(igi_sdk_tests PRIVATE igi_sdk)
target_compile_definitions(igi_sdk_tests PRIVATE IGI_SDK_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")

enable_testing()
add_test(NAME igi_sdk_tests COMMAND igi_sdk_tests)
