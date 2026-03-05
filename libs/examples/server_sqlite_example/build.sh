#!/usr/bin/env bash
set -e

function run_build {
    echo "Building ${TARGET_DIR} ..."
    mkdir -p ${TARGET_DIR}
    rm -f ${TARGET_DIR}/CMakeCache.txt
    cmake -B${TARGET_DIR} ${CMAKE_EXTRA_ARGS} .
    cmake --build ${TARGET_DIR}
}

# ------------------------
# local build
# ------------------------
if [[ "$1" == "--local" ]]; then
    export TARGET_DIR=target/local
    unset CMAKE_EXTRA_ARGS
    run_build
    exit 0
fi

# ------------------------
# wasm32-wasi build
# ------------------------
if [[ -z "${WASI_SDK_PATH}" ]]; then
    echo "WASI_SDK_PATH is required"
    exit 1
fi

export TARGET_DIR=target/wasm32-wasi

get_wasm_dependency () {
    local DEP_FILE=$1
    local DEP_URL=$2

    mkdir -p ${TARGET_DIR}/deps

    if [[ -f ${TARGET_DIR}/deps/lib/wasm32-wasi/${DEP_FILE} ]]; then
        return
    fi

    curl -sL "${DEP_URL}" | tar xz -C "${TARGET_DIR}/deps"
}

get_wasm_dependency \
  libsqlite3.a \
  https://github.com/vmware-labs/webassembly-language-runtimes/releases/download/libs%2Fsqlite%2F3.41.2%2B20230329-43f9aea/libsqlite-3.41.2-wasi-sdk-19.0.tar.gz

export FULL_TARGET_DIR=$(realpath ${TARGET_DIR})

export PKG_CONFIG_ALLOW_SYSTEM_CFLAGS=1
export PKG_CONFIG_ALLOW_SYSTEM_LIBS=1
export PKG_CONFIG_PATH=""
export PKG_CONFIG_SYSROOT_DIR=${FULL_TARGET_DIR}/deps
export PKG_CONFIG_LIBDIR=${FULL_TARGET_DIR}/deps/lib/wasm32-wasi/pkgconfig

export CMAKE_EXTRA_ARGS="
-DWASI_SDK_PREFIX=${WASI_SDK_PATH}
-DCMAKE_TOOLCHAIN_FILE=${WASI_SDK_PATH}/share/cmake/wasi-sdk.cmake
"

run_build