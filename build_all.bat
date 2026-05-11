@echo off
REM build_all.bat — Build esp-claw Desktop Simulator for all architectures
REM
REM Usage:
REM   build_all.bat              Build all three architectures
REM   build_all.bat amd64        Build amd64 only
REM   build_all.bat x86          Build x86 (32-bit) only
REM   build_all.bat arm64        Build arm64 only
REM   build_all.bat clean        Remove all build directories

setlocal EnableDelayedExpansion

set "SCRIPT_DIR=%~dp0"
cd /d "%SCRIPT_DIR%"

REM Default: build all architectures unless one is specified
if "%1"=="" (
    call :build_amd64
    if errorlevel 1 exit /b 1
    call :build_x86
    if errorlevel 1 exit /b 1
    call :build_arm64
    if errorlevel 1 exit /b 1
    goto :done
)
if /i "%1"=="amd64"  (call :build_amd64 && goto :done)
if /i "%1"=="x86"    (call :build_x86   && goto :done)
if /i "%1"=="arm64"  (call :build_arm64 && goto :done)
if /i "%1"=="clean"  (goto :clean)
echo Usage: build_all.bat [amd64^|x86^|arm64^|clean]
exit /b 1

:build_amd64
    echo.
    echo ========================================
    echo === Building amd64 (x86_64) Release ===
    echo ========================================
    set "PATH=C:\msys64\mingw64\bin;C:\msys64\usr\bin;%PATH%"
    if not exist build mkdir build
    cd build
    cmake .. -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
    if errorlevel 1 (cd .. && exit /b 1)
    mingw32-make -j%NUMBER_OF_PROCESSORS%
    if errorlevel 1 (cd .. && exit /b 1)
    cd ..
    echo amd64: OK
    goto :eof

:build_x86
    echo.
    echo ========================================
    echo === Building x86 (i686) Release ========
    echo ========================================
    set "PATH=C:\msys64\mingw32\bin;C:\msys64\usr\bin;%PATH%"
    if not exist build-x86 mkdir build-x86
    cd build-x86
    cmake .. -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
    if errorlevel 1 (cd .. && exit /b 1)
    mingw32-make -j%NUMBER_OF_PROCESSORS%
    if errorlevel 1 (cd .. && exit /b 1)
    cd ..
    echo x86: OK
    goto :eof

:build_arm64
    echo.
    echo ========================================
    echo === Building arm64 (aarch64) Release ===
    echo ========================================
    REM Uses mingw64-native clang as host compiler, targeting aarch64-w64-mingw32.
    REM Sysroot (CRT + Windows headers): C:\msys64\opt\aarch64-w64-mingw32
    REM Target libs (SDL2,curl,lua,...):    C:\msys64\clangarm64\lib
    REM Resource dir (compiler-rt builtins): C:\msys64\usr\lib\clang\21
    set "PATH=C:\msys64\mingw64\bin;C:\msys64\usr\bin;%PATH%"
    if not exist build-arm64 mkdir build-arm64
    cd build-arm64
    cmake .. -G "MinGW Makefiles" ^
        -DCMAKE_C_COMPILER=clang ^
        -DCMAKE_C_COMPILER_TARGET=aarch64-w64-mingw32 ^
        -DCROSS_AARCH64=1 ^
        -DCMAKE_C_FLAGS="--sysroot=C:/msys64/opt/aarch64-w64-mingw32 --rtlib=compiler-rt -resource-dir C:/msys64/usr/lib/clang/21" ^
        -DCMAKE_EXE_LINKER_FLAGS="-fuse-ld=lld -LC:/msys64/clangarm64/lib" ^
        -DCMAKE_BUILD_TYPE=Release
    if errorlevel 1 (cd .. && exit /b 1)
    mingw32-make -j%NUMBER_OF_PROCESSORS%
    if errorlevel 1 (cd .. && exit /b 1)
    cd ..
    echo arm64: OK
    goto :eof

:clean
    if exist build rmdir /s /q build
    if exist build-x86 rmdir /s /q build-x86
    if exist build-arm64 rmdir /s /q build-arm64
    echo All build directories cleaned.
    goto :eof

:done
    echo.
    echo ========================================
    echo === All builds complete ================
    echo ========================================
