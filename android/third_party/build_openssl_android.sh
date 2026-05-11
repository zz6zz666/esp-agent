#!/bin/bash
# build_openssl_android.sh — Cross-compile OpenSSL for Android NDK
# Usage: bash build_openssl_android.sh
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
SRC_DIR="$SCRIPT_DIR/src/openssl-3.4.1"
INSTALL_DIR="$SCRIPT_DIR/installed/openssl"
export ANDROID_NDK_ROOT="/d/Android_SDK/ndk/27.0.12077973"
export ANDROID_API_LEVEL=24
TOOLCHAIN="$ANDROID_NDK_ROOT/toolchains/llvm/prebuilt/windows-x86_64"
export PATH="$TOOLCHAIN/bin:$PATH"

build_abi() {
    local abi=$1
    local openssl_target=$2
    local cc_prefix=$3
    
    echo "=== Building OpenSSL for $abi ==="
    cd "$SRC_DIR"
    make clean 2>/dev/null || true
    
    export CC="${TOOLCHAIN}/bin/${cc_prefix}${ANDROID_API_LEVEL}-clang"
    export AR="${TOOLCHAIN}/bin/llvm-ar"
    export RANLIB="${TOOLCHAIN}/bin/llvm-ranlib"
    
    ./Configure "$openssl_target" \
        -D__ANDROID_API__=$ANDROID_API_LEVEL \
        no-shared \
        no-tests \
        --prefix="$INSTALL_DIR/$abi" \
        --openssldir="$INSTALL_DIR/$abi/ssl"
    
    make -j$(nproc 2>/dev/null || echo 4)
    make install_sw
    echo "=== OpenSSL $abi done ==="
}

mkdir -p "$INSTALL_DIR"

build_abi "arm64-v8a"   "android-arm64"   "aarch64-linux-android"
build_abi "armeabi-v7a" "android-arm"     "armv7a-linux-androideabi"
build_abi "x86_64"      "android-x86_64"  "x86_64-linux-android"

echo "=== All OpenSSL ABIs built ==="
ls "$INSTALL_DIR"/*/lib/libssl.a "$INSTALL_DIR"/*/lib/libcrypto.a
