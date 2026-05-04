@echo off
REM _run_desktop.bat — Quick launcher for esp-claw Desktop Simulator on Windows
REM
REM Usage:
REM   _run_desktop.bat run         Build and run in foreground
REM   _run_desktop.bat build       Build only (Release)
REM   _run_desktop.bat debug       Build only (Debug)
REM   _run_desktop.bat clean       Remove build directory
REM   _run_desktop.bat daemon      Run as background daemon

setlocal EnableDelayedExpansion

set "SCRIPT_DIR=%~dp0"
cd /d "%SCRIPT_DIR%"

if "%1"=="" goto :run
if /i "%1"=="run" goto :run
if /i "%1"=="build" goto :build
if /i "%1"=="debug" goto :debug
if /i "%1"=="clean" goto :clean
if /i "%1"=="daemon" goto :daemon
if /i "%1"=="-h" goto :help
if /i "%1"=="--help" goto :help
goto :help

:run
    echo === esp-claw Desktop Simulator ===
    echo Building...
    call :build_release
    if errorlevel 1 exit /b 1
    echo.
    echo Starting in foreground...
    build\esp-claw-desktop.exe --foreground %2 %3 %4 %5
    goto :eof

:build
    call :build_release
    goto :eof

:debug
    call :build_debug
    goto :eof

:build_release
    if not exist build mkdir build
    cd build
    cmake .. -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
    if errorlevel 1 (cd .. && exit /b 1)
    mingw32-make -j%NUMBER_OF_PROCESSORS%
    if errorlevel 1 (cd .. && exit /b 1)
    cd ..
    echo Done.
    goto :eof

:build_debug
    if not exist build mkdir build
    cd build
    cmake .. -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Debug
    if errorlevel 1 (cd .. && exit /b 1)
    mingw32-make -j%NUMBER_OF_PROCESSORS%
    if errorlevel 1 (cd .. && exit /b 1)
    cd ..
    echo Done.
    goto :eof

:clean
    if exist build rmdir /s /q build
    echo Build directory cleaned.
    goto :eof

:daemon
    call :build_release
    if errorlevel 1 exit /b 1
    echo Starting daemon...
    start "" build\esp-claw-desktop.exe --daemon
    echo Daemon started.
    goto :eof

:help
    echo Usage: _run_desktop.bat [command] [args...]
    echo.
    echo Commands:
    echo   run        Build and run in foreground
    echo   build      Build (Release)
    echo   debug      Build (Debug)
    echo   clean      Remove build directory
    echo   daemon     Run as background daemon
    echo.
    echo For management commands (start/stop/status/config etc):
    echo   esp-agent.exe ^<command^>
    goto :eof
