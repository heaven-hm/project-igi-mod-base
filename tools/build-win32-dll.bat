@echo off
rem ===========================================================================
rem  Build the Windows x86 (Win32) SDK artefacts natively on Windows.
rem
rem  Counterpart to build-win32-dll.sh (Wine/Linux/macOS cross-build).
rem  The .sh script cross-compiles with mingw-w64 from a Unix host. This .bat
rem  runs CMake on Windows and picks whichever 32-bit toolchain is present:
rem    - MinGW-w64 (i686-w64-mingw32-gcc) if on PATH
rem    - MSVC x86 generator otherwise
rem
rem  usage: build-win32-dll.bat [--debug^|--release] [--target cmake-target] [--clean]
rem ===========================================================================
setlocal EnableDelayedExpansion

set "ROOT=%~dp0.."
set "CONFIGURATION=Debug"
set "TARGET=igi_menu_manager_internal"
set "CLEAN=0"

:parse_args
if "%~1"=="" goto args_done
if /I "%~1"=="--debug"   ( set "CONFIGURATION=Debug"   & shift & goto parse_args )
if /I "%~1"=="--release" ( set "CONFIGURATION=Release" & shift & goto parse_args )
if /I "%~1"=="--clean"   ( set "CLEAN=1"               & shift & goto parse_args )
if /I "%~1"=="--target"  (
    set "TARGET=%~2"
    shift & shift & goto parse_args
)
if /I "%~1"=="--help" (
    echo usage: %~nx0 [--debug^|--release] [--target cmake-target] [--clean]
    exit /b 0
)
echo unknown argument: %~1 1>&2
exit /b 2
:args_done

set "BUILD_DIR=%ROOT%\build-win32-%CONFIGURATION%"

if "%CLEAN%"=="1" (
    if exist "%BUILD_DIR%" rmdir /s /q "%BUILD_DIR%"
)

rem A cache created with a different compiler cannot be converted in place.
if exist "%BUILD_DIR%\CMakeCache.txt" (
    findstr /C:"CMAKE_C_COMPILER:" "%BUILD_DIR%\CMakeCache.txt" 2>nul | findstr /I "mingw cl" >nul
    if errorlevel 1 (
        echo discarding stale build tree: %BUILD_DIR%
        rmdir /s /q "%BUILD_DIR%"
    )
)

set "MINGW_CC="
where i686-w64-mingw32-gcc >nul 2>nul && set "MINGW_CC=i686-w64-mingw32-gcc"

if defined MINGW_CC (
    echo using MinGW-w64 i686 toolchain
    cmake -S "%ROOT%" -B "%BUILD_DIR%" ^
        -G "MinGW Makefiles" ^
        -DCMAKE_SYSTEM_NAME=Windows ^
        -DCMAKE_SYSTEM_PROCESSOR=x86 ^
        -DCMAKE_C_COMPILER=i686-w64-mingw32-gcc ^
        -DCMAKE_CXX_COMPILER=i686-w64-mingw32-g++ ^
        -DCMAKE_RC_COMPILER=i686-w64-mingw32-windres ^
        -DCMAKE_BUILD_TYPE=%CONFIGURATION%
    if errorlevel 1 (
        echo error: CMake configure failed 1>&2
        exit /b 1
    )
    cmake --build "%BUILD_DIR%" --parallel --target %TARGET%
    if errorlevel 1 (
        echo error: build failed 1>&2
        exit /b 1
    )
) else (
    echo using MSVC x86 toolchain
    cmake -S "%ROOT%" -B "%BUILD_DIR%" -A Win32 -DCMAKE_BUILD_TYPE=%CONFIGURATION%
    if errorlevel 1 (
        echo error: CMake configure failed 1>&2
        echo hint: install MinGW-w64 (i686) or Visual Studio x86 tools 1>&2
        exit /b 1
    )
    cmake --build "%BUILD_DIR%" --config %CONFIGURATION% --parallel --target %TARGET%
    if errorlevel 1 (
        echo error: build failed 1>&2
        exit /b 1
    )
)

echo built: %BUILD_DIR%
endlocal
exit /b 0
