#!/bin/bash
# build_libpng_android.sh — Cross-compile libpng for Android NDK
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
INSTDIR="$SCRIPT_DIR/installed"
SRCDIR="$SCRIPT_DIR/src/libpng-1.6.47"
NDK="/d/Android_SDK/ndk/27.0.12077973"
SYSROOT="$NDK/toolchains/llvm/prebuilt/windows-x86_64/sysroot"

build_abi() {
    local abi=$1
    local zlib_arch=$2
    local prefix="$INSTDIR/libpng/$abi"
    rm -rf "$SRCDIR/build_$abi"
    mkdir -p "$SRCDIR/build_$abi"
    cd "$SRCDIR/build_$abi"

    cmake .. \
        -DCMAKE_TOOLCHAIN_FILE="$NDK/build/cmake/android.toolchain.cmake" \
        -DANDROID_ABI="$abi" \
        -DANDROID_PLATFORM=android-24 \
        -DCMAKE_BUILD_TYPE=Release \
        -DBUILD_SHARED_LIBS=OFF \
        -DPNG_TESTS=OFF \
        -DPNG_TOOLS=OFF \
        -DPNG_SHARED=OFF \
        -DPNG_STATIC=ON \
        -DCMAKE_INSTALL_PREFIX="$prefix" \
        -DZLIB_ROOT="$SYSROOT/usr" \
        -DZLIB_LIBRARY="$SYSROOT/usr/lib/$zlib_arch/24/libz.so" \
        -DZLIB_INCLUDE_DIR="$SYSROOT/usr/include"

    cmake --build . -j8
    cmake --install .
    echo "=== libpng $abi done ==="
}

build_abi "arm64-v8a"    "aarch64-linux-android"
build_abi "armeabi-v7a"  "arm-linux-androideabi"
build_abi "x86_64"       "x86_64-linux-android"

echo "=== All libpng ABIs built ==="
ls "$INSTDIR/libpng"/*/lib/libpng16.a
