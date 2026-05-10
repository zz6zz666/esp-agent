@echo off
REM package.bat — Package Crush Claw for Windows distribution
REM
REM Creates a self-contained package with:
REM   esp-claw-desktop.exe  — the agent daemon
REM   crush-claw.exe        — the CLI management tool
REM   fonts/                — DejaVuSans.ttf, NotoColorEmoji.ttf
REM   defaults/             — default config, skills, router rules

setlocal EnableDelayedExpansion

set "SCRIPT_DIR=%~dp0"
cd /d "%SCRIPT_DIR%"

set "VERSION=1.1.0"
set "PKG_NAME=crush-claw-windows-v%VERSION%"

echo === Packaging Crush Claw v%VERSION% for Windows ===

REM Clean and build
call :build
if errorlevel 1 exit /b 1

REM Create package directory
if exist "%PKG_NAME%" rmdir /s /q "%PKG_NAME%"
mkdir "%PKG_NAME%"
mkdir "%PKG_NAME%\fonts"
mkdir "%PKG_NAME%\defaults"
mkdir "%PKG_NAME%\defaults\skills"
mkdir "%PKG_NAME%\defaults\router_rules"
mkdir "%PKG_NAME%\defaults\scheduler"

REM Copy binaries
echo Copying binaries...
copy /y "build\esp-claw-desktop.exe" "%PKG_NAME%\"
if errorlevel 1 (echo Failed to copy esp-claw-desktop.exe && exit /b 1)

copy /y "build\crush-claw.exe" "%PKG_NAME%\"
if errorlevel 1 (echo Failed to copy crush-claw.exe && exit /b 1)

REM Copy fonts (from packaging dir or fetch)
echo Copying fonts...
if exist "fonts\DejaVuSans.ttf" (
    copy /y "fonts\DejaVuSans.ttf" "%PKG_NAME%\fonts\"
)
if exist "fonts\NotoColorEmoji.ttf" (
    copy /y "fonts\NotoColorEmoji.ttf" "%PKG_NAME%\fonts\"
)
if exist "fonts\arial.ttf" (
    copy /y "fonts\arial.ttf" "%PKG_NAME%\fonts\"
)
if exist "fonts\segoeui.ttf" (
    copy /y "fonts\segoeui.ttf" "%PKG_NAME%\fonts\"
)

REM Warn if no fonts were found
dir /b "%PKG_NAME%\fonts\*.*" >nul 2>&1
if errorlevel 1 (
    echo WARNING: No fonts found in fonts/ directory.
    echo You should place DejaVuSans.ttf and NotoColorEmoji.ttf in the fonts/ subdirectory
    echo before distributing. Fonts are loaded from %%EXE_DIR%%\fonts\ at runtime.
)

REM Copy default config and data files
echo Copying defaults...
if exist "packaging\usr\share\crush-claw\defaults\*" (
    xcopy /e /y "packaging\usr\share\crush-claw\defaults\*" "%PKG_NAME%\defaults\"
    echo   Defaults copied from packaging/
) else (
    echo   No packaging defaults found, using source defaults

    REM Copy skills from defaults if exists
    if exist "defaults\skills\*" (
        xcopy /e /y "defaults\skills\*" "%PKG_NAME%\defaults\skills\"
    )

    REM Copy other default files
    if exist "defaults\config.json" copy /y "defaults\config.json" "%PKG_NAME%\defaults\"
)

REM Create README
echo Creating README...
(
    echo Crush Claw v%VERSION% for Windows
    echo ================================
    echo.
    echo Quick Start:
    echo   1. Run crush-claw.exe config   to configure LLM keys
    echo   2. Run crush-claw.exe start    to start the agent
    echo   3. Run crush-claw.exe ask "Hello"  to test
    echo.
    echo Files:
    echo   esp-claw-desktop.exe  — The agent daemon
    echo   crush-claw.exe        — CLI management tool
    echo   fonts/                — Font files for display rendering
    echo   defaults/             — Default configuration
    echo.
    echo Data is stored in: %%USERPROFILE%%\.crush-claw\
    echo.
    echo For more info, see the project README.
) > "%PKG_NAME%\README.txt"

REM Package as ZIP
echo Creating ZIP archive...
powershell -Command "Compress-Archive -Path '%PKG_NAME%' -DestinationPath '%PKG_NAME%.zip' -Force"
if errorlevel 1 (
    echo Failed to create ZIP, but package directory is ready: %PKG_NAME%
) else (
    echo Package: %PKG_NAME%.zip
)

echo.
echo Done! Package contents:
dir "%PKG_NAME%\"
echo.
echo To distribute: send %PKG_NAME%.zip
goto :eof

:build
    echo Building...
    if not exist build mkdir build
    cd build
    cmake .. -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
    if errorlevel 1 (cd .. && exit /b 1)
    mingw32-make -j%NUMBER_OF_PROCESSORS%
    if errorlevel 1 (cd .. && exit /b 1)
    cd ..
    goto :eof
