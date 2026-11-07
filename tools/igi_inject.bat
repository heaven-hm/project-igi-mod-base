@echo off
rem ===========================================================================
rem  Inject the SDK DLL into a running IGI process natively on Windows.
rem
rem  Counterpart to wine_igi_inject.sh. The .sh script translates paths through
rem  winepath; this .bat runs the injector directly against the native process.
rem
rem  usage: igi_inject.bat ^<dll^> [exe-name] [injector.exe]
rem ===========================================================================
setlocal EnableDelayedExpansion

set "DLL=%~1"
set "EXE_NAME=%~2"
if "%EXE_NAME%"=="" set "EXE_NAME=igi.exe"
set "INJECTOR=%~3"

if "%DLL%"=="" (
    echo usage: %~nx0 ^<dll^> [exe-name] [injector.exe] 1>&2
    exit /b 2
)
if not exist "%DLL%" (
    echo error: missing DLL: %DLL% 1>&2
    exit /b 2
)

if "%INJECTOR%"=="" (
    for %%C in (
        "%~dp0IGI-Injector.exe"
        "%~dp0..\build-win32-Debug\IGI-Injector.exe"
        "%~dp0..\build-win32-Release\IGI-Injector.exe"
        "%~dp0..\build-win32-debug\IGI-Injector.exe"
        "%~dp0..\build-win32-release\IGI-Injector.exe"
    ) do (
        if exist %%C (
            set "INJECTOR=%%~C"
            goto injector_found
        )
    )
)
:injector_found

if "%INJECTOR%"=="" (
    echo error: injector not found; build it with: 1>&2
    echo   cmake --build ^<dir^> --target igi_injector 1>&2
    exit /b 2
)
if not exist "%INJECTOR%" (
    echo error: missing injector: %INJECTOR% 1>&2
    exit /b 2
)

echo injecting %DLL% into %EXE_NAME%
"%INJECTOR%" -n "%EXE_NAME%" -i "%DLL%" -v
exit /b %errorlevel%
