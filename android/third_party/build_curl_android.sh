#!/bin/bash
# build_curl_android.sh — Cross-compile libcurl for Android NDK
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
INSTDIR="$SCRIPT_DIR/installed"
SRCDIR="$SCRIPT_DIR/src/curl-8.14.1"
NDK="/d/Android_SDK/ndk/27.0.12077973"
SYSROOT="$NDK/toolchains/llvm/prebuilt/windows-x86_64/sysroot"

build_abi() {
    local abi=$1
    local zlib_arch=$2
    local OSSLDIR="$INSTDIR/openssl/$abi"
    local PREFIX="$INSTDIR/curl/$abi"
    local build_dir="build_$abi"

    rm -rf "$SRCDIR/$build_dir"
    mkdir -p "$SRCDIR/$build_dir"
    cd "$SRCDIR/$build_dir"

    cmake .. \
        -DCMAKE_TOOLCHAIN_FILE="$NDK/build/cmake/android.toolchain.cmake" \
        -DANDROID_ABI="$abi" \
        -DANDROID_PLATFORM=android-24 \
        -DCMAKE_BUILD_TYPE=Release \
        -DBUILD_SHARED_LIBS=OFF \
        -DCURL_DISABLE_LDAP=ON \
        -DCURL_DISABLE_LDAPS=ON \
        -DCURL_USE_LIBPSL=OFF \
        -DHTTP_ONLY=ON \
        -DBUILD_CURL_EXE=OFF \
        -DBUILD_TESTING=OFF \
        -DOPENSSL_ROOT_DIR="$OSSLDIR" \
        -DOPENSSL_CRYPTO_LIBRARY="$OSSLDIR/lib/libcrypto.a" \
        -DOPENSSL_SSL_LIBRARY="$OSSLDIR/lib/libssl.a" \
        -DOPENSSL_INCLUDE_DIR="$OSSLDIR/include" \
        -DOPENSSL_USE_STATIC_LIBS=ON \
        -DCMAKE_INSTALL_PREFIX="$PREFIX" \
        -DZLIB_LIBRARY="$SYSROOT/usr/lib/$zlib_arch/24/libz.so" \
        -DZLIB_INCLUDE_DIR="$SYSROOT/usr/include"

    cmake --build . -j8
    cmake --install .
    echo "=== libcurl $abi done ==="
}

build_abi "arm64-v8a"    "aarch64-linux-android"
build_abi "armeabi-v7a"  "arm-linux-androideabi"
build_abi "x86_64"       "x86_64-linux-android"

echo "=== All libcurl ABIs built ==="
ls "$INSTDIR/curl"/*/lib/libcurl.a
