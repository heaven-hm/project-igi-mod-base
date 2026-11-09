@echo off
setlocal EnableExtensions
set "CONFIG=Release"
set "DEMOS=ON"
set "BUILD_DIR=%TEMP%\igi-sdk-windows-%CONFIG%"

:parse
if "%~1"=="" goto configure
if /I "%~1"=="--build" (
  if /I not "%~2"=="windows" (echo Use tools/build.sh for macOS/Linux.& exit /b 2)
  shift & shift & goto parse
)
if /I "%~1"=="--config" (set "CONFIG=%~2"& set "BUILD_DIR=%TEMP%\igi-sdk-windows-%~2"& shift & shift & goto parse)
if /I "%~1"=="--no-demos" (set "DEMOS=OFF"& shift& goto parse)
if /I "%~1"=="--clean" (if exist "%BUILD_DIR%" rmdir /s /q "%BUILD_DIR%"& shift& goto parse)
if /I "%~1"=="--help" goto usage
echo Unknown argument: %~1
goto usage_error

:configure
cmake -S "%~dp0." -B "%BUILD_DIR%" -DCMAKE_BUILD_TYPE=%CONFIG% -DIGI_SDK_BUILD_DEMOS=%DEMOS%
if errorlevel 1 exit /b %errorlevel%
cmake --build "%BUILD_DIR%" --config %CONFIG% --parallel
if errorlevel 1 exit /b %errorlevel%
ctest --test-dir "%BUILD_DIR%" -C %CONFIG% --output-on-failure
if errorlevel 1 exit /b %errorlevel%
echo Build complete: %BUILD_DIR%
exit /b 0

:usage
echo Usage: build.cmd [--build windows] [--config Debug^|Release] [--no-demos] [--clean]
exit /b 0

:usage_error
call :usage
exit /b 2
