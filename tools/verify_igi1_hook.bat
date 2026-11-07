@echo off
rem ===========================================================================
rem  Verify the IGI 1 Game_RunHandler hook evidence natively on Windows.
rem
rem  Counterpart to verify_igi1_hook.sh. Uses certutil for the SHA-256 and the
rem  mingw objdump (or set IGI_OBJDUMP) for the disassembly checks.
rem
rem  usage: verify_igi1_hook.bat ^<path\to\igi.exe^>   (or set IGI_EXE)
rem ===========================================================================
setlocal EnableDelayedExpansion

set "BINARY=%~1"
if "%BINARY%"=="" set "BINARY=%IGI_EXE%"
if "%BINARY%"=="" (
    echo usage: %~nx0 ^<path\to\igi.exe^> ^(or set IGI_EXE^) 1>&2
    exit /b 2
)
set "EXPECTED_HASH=dea34892d417715873ea61d7b2ba5556655f1ad65385104348146f9679915a01"
set "OBJDUMP=%IGI_OBJDUMP%"
if "%OBJDUMP%"=="" set "OBJDUMP=i686-w64-mingw32-objdump"

where %OBJDUMP% >nul 2>nul
if errorlevel 1 (
    echo missing PE i386 objdump; set IGI_OBJDUMP 1>&2
    exit /b 1
)
if not exist "%BINARY%" (
    echo missing target executable: %BINARY% 1>&2
    exit /b 1
)

rem certutil prints the hash on the second line of output.
set "ACTUAL_HASH="
for /f "skip=1 delims=" %%H in ('certutil -hashfile "%BINARY%" SHA256 2^>nul') do (
    if not defined ACTUAL_HASH set "ACTUAL_HASH=%%H"
)
set "ACTUAL_HASH=%ACTUAL_HASH: =%"
if /I not "%ACTUAL_HASH%"=="%EXPECTED_HASH%" (
    echo hash mismatch: %ACTUAL_HASH% 1>&2
    exit /b 1
)
echo verified SHA-256: %ACTUAL_HASH%

call :check 0x00416260 0x00416270 "416260:.*push *ebx" || goto :game_run_fail
call :check 0x00416260 0x00416270 "416261:.*push *esi" || goto :game_run_fail
call :check 0x00416260 0x00416270 "416262:.*push *edi" || goto :game_run_fail
call :check 0x00416260 0x00416270 "416263:.*call *0x4163e0" || goto :game_run_fail

call :check 0x00402260 0x00402270 "402260:.*sub *esp,0x8" || goto :flow_run_fail
call :check 0x00402260 0x00402270 "402263:.*push *ebx" || goto :flow_run_fail
call :check 0x00402260 0x00402270 "402264:.*push *ebp" || goto :flow_run_fail

call :check 0x00401050 0x00401090 "401072:.*mov *esi,DWORD PTR \[ecx\*4\+0xa970e0\]" || goto :game_update_fail
call :check 0x00401050 0x00401090 "40107f:.*call *esi" || goto :game_update_fail
call :check 0x00401050 0x00401090 "401084:.*pop *esi" || goto :game_update_fail
call :check 0x00401050 0x00401090 "401086:.*ret" || goto :game_update_fail

echo verified Game_RunHandler body and cdecl-preserving return path
echo verified Flow_RunHandler as secondary flow-lifecycle evidence
echo verified Game_Update indirect task dispatch; it is not selected as a hook
endlocal
exit /b 0

:game_run_fail
echo Game_RunHandler body/prologue mismatch 1>&2
exit /b 1

:flow_run_fail
echo Flow_RunHandler body/prologue mismatch 1>&2
exit /b 1

:game_update_fail
echo Game_Update task dispatch/calling-convention mismatch 1>&2
exit /b 1

:check
rem %1 start address, %2 stop address, %3 regex to find
"%OBJDUMP%" -D -Mintel --start-address=%1 --stop-address=%2 "%BINARY%" 2>nul | findstr /R /C:"%~3" >nul
exit /b %errorlevel%
