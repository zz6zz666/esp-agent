#!/bin/bash
# build_freetype_android.sh — Cross-compile FreeType for Android NDK
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
INSTDIR="$SCRIPT_DIR/installed"
SRCDIR="$SCRIPT_DIR/src/freetype-2.14.3"
NDK="/d/Android_SDK/ndk/27.0.12077973"

mkdir -p "$INSTDIR"

build_abi() {
    local abi=$1
    local prefix="$INSTDIR/freetype/$abi"
    rm -rf "$SRCDIR/build_$abi"
    mkdir -p "$SRCDIR/build_$abi"
    cd "$SRCDIR/build_$abi"

    cmake .. \
        -DCMAKE_TOOLCHAIN_FILE="$NDK/build/cmake/android.toolchain.cmake" \
        -DANDROID_ABI="$abi" \
        -DANDROID_PLATFORM=android-24 \
        -DCMAKE_BUILD_TYPE=Release \
        -DBUILD_SHARED_LIBS=OFF \
        -DFT_DISABLE_BZIP2=ON \
        -DFT_DISABLE_HARFBUZZ=ON \
        -DFT_DISABLE_BROTLI=ON \
        -DPNG_PNG_INCLUDE_DIR="$INSTDIR/libpng/$abi/include" \
        -DPNG_LIBRARY="$INSTDIR/libpng/$abi/lib/libpng16.a" \
        -DCMAKE_INSTALL_PREFIX="$prefix"

    cmake --build . -j8
    cmake --install .
    echo "=== FreeType $abi done ==="
}

build_abi "arm64-v8a"
build_abi "armeabi-v7a"
build_abi "x86_64"

echo "=== All FreeType ABIs built ==="
ls "$INSTDIR/freetype"/*/lib/libfreetype.a
