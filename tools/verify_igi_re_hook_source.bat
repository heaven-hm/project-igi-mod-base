@echo off
rem ===========================================================================
rem  Verify IGI-RE source registration and signatures natively on Windows.
rem
rem  Counterpart to verify_igi_re_hook_source.sh. Uses findstr instead of grep.
rem
rem  usage: verify_igi_re_hook_source.bat ^<path\to\IGI-RE^>   (or set IGI_RE_ROOT)
rem ===========================================================================
setlocal EnableDelayedExpansion

set "IGI_RE_DIR=%~1"
if "%IGI_RE_DIR%"=="" set "IGI_RE_DIR=%IGI_RE_ROOT%"
if "%IGI_RE_DIR%"=="" (
    echo usage: %~nx0 ^<path\to\IGI-RE^> ^(or set IGI_RE_ROOT^) 1>&2
    exit /b 2
)

set "GAME=%IGI_RE_DIR%\src\game\game.c"
set "FLOW=%IGI_RE_DIR%\src\flow\flow.c"
set "FLOW_H=%IGI_RE_DIR%\src\flow\flow.h"
set "JUMP=%IGI_RE_DIR%\src\flow\jump.c"

for %%F in ("%GAME%" "%FLOW%" "%FLOW_H%" "%JUMP%") do (
    if not exist %%F (
        echo missing IGI-RE source file: %%F 1>&2
        exit /b 1
    )
)

set "FAILED=0"
call :expect "TaskType_SetHandler2(gGameType, 1, Game_RunHandler);" "%GAME%" || set "FAILED=1"
call :expect "Script_RegisterConstant(\"GAME_FREQUENCY\", 30);" "%GAME%" || set "FAILED=1"
call :expect "void Game_RunHandler(Game *g)" "%GAME%" || set "FAILED=1"
call :expect "HOOK(0x416260, Game_RunHandler);" "%GAME%" || set "FAILED=1"
call :expect "void Flow_RunHandler(Flow *flow)" "%FLOW%" || set "FAILED=1"
call :expect "HOOK(0x402260, Flow_RunHandler);" "%FLOW%" || set "FAILED=1"
call :expect "void Flow_RunHandler(Flow *flow);" "%FLOW_H%" || set "FAILED=1"
call :expect "typedef void (*GameRunHandler)(void *task);" "%JUMP%" || set "FAILED=1"
call :expect "run = gTaskRunHandlers[*(uint16_t *)((char *)Flow_Get() + 0x1C)];" "%JUMP%" || set "FAILED=1"
call :expect "run(Flow_Get());" "%JUMP%" || set "FAILED=1"
call :expect "HOOK(0x401050, Game_Update);" "%JUMP%" || set "FAILED=1"

if "%FAILED%"=="1" (
    echo IGI-RE source verification failed 1>&2
    exit /b 1
)

echo verified IGI-RE source registration and signatures
echo verified Game_RunHandler cadence source: GAME_FREQUENCY=30
echo verified Game_Update dispatches the active task-slot-1 handler
endlocal
exit /b 0

:expect
rem %1 literal text to find, %2 file to search
findstr /C:"%~1" "%~2" >nul 2>nul
if errorlevel 1 (
    echo missing in %~2: %~1 1>&2
    exit /b 1
)
exit /b 0
