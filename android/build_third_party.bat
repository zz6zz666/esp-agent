@echo off
setlocal enabledelayedexpansion

REM ============================================================
REM build_third_party.bat — Cross-compile lua + json-c for Android
REM Dependencies: Android NDK 27 (set via ANDROID_HOME)
REM ============================================================

set NDK=%ANDROID_HOME%\ndk\27.0.12077973
set TOOLCHAIN=%NDK%\toolchains\llvm\prebuilt\windows-x86_64
set THIRD_PARTY=%~dp0..\third_party
set INSTALLED=%THIRD_PARTY%\installed

REM ABIs to build for (matching Android app's abiFilters)
set ABI[0]=arm64-v8a
set ABI[1]=armeabi-v7a
set ABI[2]=x86_64

set HOST_TAG=windows-x86_64
set MIN_API=26

echo === Building third-party libraries for Android ===
echo NDK: %NDK%
echo.

REM ============================================================
REM 1. Lua 5.4.7
REM ============================================================
echo --- Building Lua 5.4.7 ---

for %%T in (arm64-v8a armeabi-v7a x86_64) do (
    if "%%T"=="arm64-v8a" (
        set TARGET=aarch64-linux-android
        set ARCH_DIR=arm64-v8a
    )
    if "%%T"=="armeabi-v7a" (
        set TARGET=armv7a-linux-androideabi
        set ARCH_DIR=armeabi-v7a
    )
    if "%%T"=="x86_64" (
        set TARGET=x86_64-linux-android
        set ARCH_DIR=x86_64
    )

    set PREFIX=%INSTALLED%\lua\!ARCH_DIR!
    set SRC=%THIRD_PARTY%\src\lua-5.4.7
    set BUILD_DIR=%THIRD_PARTY%\build\lua\!ARCH_DIR!

    if exist "!PREFIX!\lib\liblua.a" (
        echo [SKIP] Lua !ARCH_DIR! — already built
    ) else (
        echo [BUILD] Lua !ARCH_DIR!

        REM Create clean build directory
        if exist "!BUILD_DIR!" rmdir /s /q "!BUILD_DIR!"
        mkdir "!BUILD_DIR!"

        cd "!SRC!"

        REM Cross-compile using NDK toolchain
        set CC=!TOOLCHAIN!\bin\!TARGET!%MIN_API%-clang
        set AR=!TOOLCHAIN!\bin\llvm-ar
        set RANLIB=!TOOLCHAIN!\bin\llvm-ranlib
        set CFLAGS=-O2 -fPIC -DLUA_USE_LINUX -DLUAI_HASHLIMIT=5

        REM Find the correct make command
        if exist "C:\msys64\usr\bin\make.exe" (set MAKE="C:\msys64\usr\bin\make.exe") else (set MAKE=make)

        !MAKE! -C "!SRC!" clean 2>nul
        !MAKE! -C "!SRC!" -j4 ^
            CC="!CC!" ^
            AR="!AR! rcu" ^
            RANLIB="!RANLIB!" ^
            CFLAGS="!CFLAGS!" ^
            CFLAGS_S="" ^
            LDFLAGS="-Wl,--build-id" ^
            LIBS="" ^
            SYSCFLAGS="-DLUA_USE_LINUX" ^
            SYSLIBS="" ^
            a 2>&1

        if errorlevel 1 (
            echo [FAIL] Lua !ARCH_DIR! — compilation failed
            exit /b 1
        )

        REM Install
        mkdir "!PREFIX!\lib" "!PREFIX!\include" 2>nul
        copy /y "!SRC!\lua.h" "!PREFIX!\include\"
        copy /y "!SRC!\luaconf.h" "!PREFIX!\include\"
        copy /y "!SRC!\lualib.h" "!PREFIX!\include\"
        copy /y "!SRC!\lauxlib.h" "!PREFIX!\include\"
        copy /y "!SRC!\lua.hpp" "!PREFIX!\include\"
        copy /y "!SRC!\liblua.a" "!PREFIX!\lib\"

        echo [OK] Lua !ARCH_DIR! built: !PREFIX!
    )
)

echo.
echo Lua build complete
echo.

REM ============================================================
REM 2. json-c (via CMake + NDK toolchain)
REM ============================================================
echo --- Building json-c ---

set JSONC_SRC=%THIRD_PARTY%\src\json-c-json-c-0.17-20230812

for %%T in (arm64-v8a armeabi-v7a x86_64) do (
    if "%%T"=="arm64-v8a" (set ARCH_DIR=arm64-v8a)
    if "%%T"=="armeabi-v7a" (set ARCH_DIR=armeabi-v7a)
    if "%%T"=="x86_64" (set ARCH_DIR=x86_64)

    set PREFIX=%INSTALLED%\json-c\!ARCH_DIR!
    set BUILD_DIR=%THIRD_PARTY%\build\json-c\!ARCH_DIR!

    if exist "!PREFIX!\lib\libjson-c.a" (
        echo [SKIP] json-c !ARCH_DIR! — already built
    ) else (
        echo [BUILD] json-c !ARCH_DIR!

        if exist "!BUILD_DIR!" rmdir /s /q "!BUILD_DIR!"
        mkdir "!BUILD_DIR!"

        cd "!BUILD_DIR!"

        cmake ^
            -GNinja ^
            -DCMAKE_TOOLCHAIN_FILE=%NDK%\build\cmake\android.toolchain.cmake ^
            -DANDROID_ABI=%%T ^
            -DANDROID_PLATFORM=android-%MIN_API% ^
            -DCMAKE_INSTALL_PREFIX=!PREFIX! ^
            -DBUILD_SHARED_LIBS=OFF ^
            -DBUILD_TESTS=OFF ^
            -DDISABLE_THREAD_LOCAL=ON ^
            -DCMAKE_BUILD_TYPE=Release ^
            "!JSONC_SRC!" 2>&1

        if errorlevel 1 (
            echo [FAIL] json-c !ARCH_DIR! — cmake configure failed
            exit /b 1
        )

        ninja install 2>&1
        if errorlevel 1 (
            echo [FAIL] json-c !ARCH_DIR! — ninja failed
            exit /b 1
        )

        echo [OK] json-c !ARCH_DIR! built: !PREFIX!
    )
)

echo.
echo === All third-party libraries built successfully ===
