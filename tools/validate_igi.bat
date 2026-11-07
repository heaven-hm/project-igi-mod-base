@echo off
rem ===========================================================================
rem  Rebuild and exercise the Windows x86 injected-menu path natively.
rem
rem  Counterpart to validate_wine_igi.sh. The .sh script drives the game under
rem  Wine on macOS/Linux. This .bat drives the native igi.exe on Windows:
rem  it builds, launches the game, injects, presses Escape then F5, and checks
rem  the SDK log for the menu-attach markers. Failures retry from a clean state.
rem
rem  usage: validate_igi.bat [--attempts N]
rem ===========================================================================
setlocal EnableDelayedExpansion

set "ROOT=%~dp0.."
set "GAME_DIR=%IGI_GAME_DIR%"
if "%GAME_DIR%"=="" set "GAME_DIR=%USERPROFILE%\Downloads\IGI"
set "BUILD_WIN32=%IGI_WIN32_BUILD_DIR%"
if "%BUILD_WIN32%"=="" set "BUILD_WIN32=%ROOT%\build-win32"
set "ATTEMPTS=5"

:parse_args
if "%~1"=="" goto args_done
if /I "%~1"=="--attempts" (
    set "ATTEMPTS=%~2"
    shift & shift & goto parse_args
)
if /I "%~1"=="--help" (
    echo usage: %~nx0 [--attempts N]
    exit /b 0
)
echo unknown option: %~1 1>&2
exit /b 2
:args_done

if not exist "%GAME_DIR%\igi.exe" (
    echo IGI executable not found: %GAME_DIR%\igi.exe 1>&2
    exit /b 2
)

for /f "delims=" %%L in ('python "%ROOT%\tools\igi_log_config.py" "%GAME_DIR%"') do set "SDK_LOG=%GAME_DIR%\%%L"
set "ARTIFACT_DIR=%TEMP%\igi-live-validation"
if not exist "%ARTIFACT_DIR%" mkdir "%ARTIFACT_DIR%"

echo ==^> Windows x86 build
cmake --build "%BUILD_WIN32%" --parallel --target igi_basic_mod_menu igi_injector igi_keypress
if errorlevel 1 (
    echo error: build failed 1>&2
    exit /b 1
)
copy /Y "%BUILD_WIN32%\igi-basic-mod-internal.dll" "%GAME_DIR%\igi-basic-mod-internal.dll" >nul
copy /Y "%BUILD_WIN32%\IGI-Injector.exe" "%GAME_DIR%\IGI-Injector.exe" >nul
copy /Y "%BUILD_WIN32%\IGI-Keypress.exe" "%GAME_DIR%\IGI-Keypress.exe" >nul

for /L %%A in (1,1,%ATTEMPTS%) do (
    echo ==^> Native live attempt %%A/%ATTEMPTS%
    call :attempt %%A
    if !errorlevel!==0 (
        echo SUCCESS: native launch, injection, menu attach and End-key health check passed. Evidence: %ARTIFACT_DIR%
        endlocal
        exit /b 0
    )
    echo attempt %%A failed: health check did not pass; retrying from a clean state 1>&2
    taskkill /F /IM igi.exe >nul 2>nul
    ping -n 3 127.0.0.1 >nul
)

echo FAILURE: live validation exhausted %ATTEMPTS% attempts; evidence is in %ARTIFACT_DIR% 1>&2
endlocal
exit /b 1

rem ---------------------------------------------------------------------------
rem :attempt <n>  — one launch/inject/press/verify cycle. Returns 0 on success.
rem ---------------------------------------------------------------------------
:attempt
set "N=%~1"
set "LOG=!ARTIFACT_DIR!\attempt-!N!.log"

rem A stale igi.exe would confuse PID detection, so clear it first.
taskkill /F /IM igi.exe >nul 2>nul
ping -n 2 127.0.0.1 >nul

if exist "%LOG%" del "%LOG%"

pushd "%GAME_DIR%"
start "" /B "igi.exe" window level5 >"%LOG%" 2>&1
popd

rem Wait up to ~20s for igi.exe to appear.
set "READY=0"
for /L %%W in (1,1,20) do (
    if "!READY!"=="0" (
        tasklist /FI "IMAGENAME eq igi.exe" /FO CSV /NH 2>nul | findstr /I "igi.exe" >nul
        if not errorlevel 1 (
            set "READY=1"
        ) else (
            ping -n 2 127.0.0.1 >nul
        )
    )
)
if "!READY!"=="0" (
    echo attempt !N!: igi.exe was not running 1>&2
    if exist "%LOG%" type "%LOG%" 1>&2
    exit /b 1
)

rem Read the Windows PID of igi.exe.
set "WIN_PID="
for /f "tokens=2 delims=," %%P in ('tasklist /FI "IMAGENAME eq igi.exe" /FO CSV /NH 2^>nul ^| findstr /I "igi.exe"') do (
    if not defined WIN_PID set "WIN_PID=%%~P"
)
if "%WIN_PID%"=="" (
    echo attempt !N!: could not read igi.exe PID 1>&2
    exit /b 1
)
echo attempt !N!: igi.exe Windows PID %WIN_PID%

rem Inject the DLL. The injector's own output goes to the attempt log.
"%GAME_DIR%\IGI-Injector.exe" -n "igi.exe" -i "%GAME_DIR%\igi-basic-mod-internal.dll" -v >"%LOG%" 2>&1
set "INJECTED=0"
for /L %%W in (1,1,20) do (
    if "!INJECTED!"=="0" (
        findstr /C:"injected into pid" "%LOG%" >nul 2>nul
        if not errorlevel 1 (
            set "INJECTED=1"
        ) else (
            ping -n 2 127.0.0.1 >nul
        )
    )
)
if "!INJECTED!"=="0" (
    echo attempt !N!: injector did not confirm injection 1>&2
    if exist "%LOG%" type "%LOG%" 1>&2
    exit /b 1
)

rem Dismiss the intro/cutscene, then open the mod menu.
"%GAME_DIR%\IGI-Keypress.exe" --pid "%WIN_PID%" escape >nul 2>nul
ping -n 2 127.0.0.1 >nul
"%GAME_DIR%\IGI-Keypress.exe" --pid "%WIN_PID%" f5 >nul 2>nul
ping -n 6 127.0.0.1 >nul

rem Health check: game alive AND all three SDK markers present.
set "ALIVE=0"
tasklist /FI "IMAGENAME eq igi.exe" /FO CSV /NH 2>nul | findstr /I "igi.exe" >nul
if not errorlevel 1 set "ALIVE=1"

set "MARK_AUTOSTART=0"
findstr /C:"internal validation auto-start enabled" "%SDK_LOG%" >nul 2>nul
if not errorlevel 1 set "MARK_AUTOSTART=1"

set "MARK_HOOK=0"
findstr /C:"IGI 1 Display_AddFrameCallback scheduler hook installed" "%SDK_LOG%" >nul 2>nul
if not errorlevel 1 set "MARK_HOOK=1"

set "MARK_MENU=0"
findstr /C:"internal menu screen: attached and requested" "%SDK_LOG%" >nul 2>nul
if not errorlevel 1 set "MARK_MENU=1"

if not "%ALIVE%%MARK_AUTOSTART%%MARK_HOOK%%MARK_MENU%"=="1111" (
    echo attempt !N! health check: alive=%ALIVE% autostart=%MARK_AUTOSTART% hook=%MARK_HOOK% menu=%MARK_MENU% 1>&2
    if exist "%SDK_LOG%" type "%SDK_LOG%" 1>&2
    if exist "%LOG%" type "%LOG%" 1>&2
    exit /b 1
)

echo PASS: game stayed live and demo mod-menu screen attached ^(screen_id=990^).

rem End-key health check: shutdown must not take the game down with it.
"%GAME_DIR%\IGI-Keypress.exe" --pid "%WIN_PID%" end >nul 2>nul
ping -n 3 127.0.0.1 >nul
tasklist /FI "IMAGENAME eq igi.exe" /FO CSV /NH 2>nul | findstr /I "igi.exe" >nul
if errorlevel 1 (
    echo failure: game quit during End-key shutdown 1>&2
    exit /b 1
)

exit /b 0
