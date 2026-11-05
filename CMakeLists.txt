cmake_minimum_required(VERSION 3.20)
project(project_igi_natives_sdk VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(igi_sdk STATIC
    src/IGI/Logging.cpp
    src/IGI/Scheduler.cpp
    src/IGI/Runtime.cpp
    src/IGI/HookManager.cpp
)
target_include_directories(igi_sdk PUBLIC include)
target_compile_definitions(igi_sdk PUBLIC IGI_SDK_IGI1_ONLY=1)

if(WIN32)
    target_compile_definitions(igi_sdk PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
endif()

add_executable(igi_sdk_tests tests/SchedulerTests.cpp)
target_link_libraries(igi_sdk_tests PRIVATE igi_sdk)

enable_testing()
add_test(NAME igi_sdk_tests COMMAND igi_sdk_tests)
