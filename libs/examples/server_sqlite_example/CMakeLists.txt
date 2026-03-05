cmake_minimum_required(VERSION 3.16)
project(wasm_sqlite_examples C)

if(${CMAKE_SYSTEM_NAME} STREQUAL "WASI")
    set(CMAKE_EXECUTABLE_SUFFIX ".wasm")
endif()

find_package(PkgConfig REQUIRED)
pkg_search_module(SQLITE3 REQUIRED IMPORTED_TARGET "sqlite3>=3")

# ==============================
# 共通ライブラリ（WASI socket 拡張）
# ==============================
add_library(wasi_socket STATIC
    wasi_socket_ext.c
)

target_include_directories(wasi_socket PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# ==============================
# Cloud DB Server
# ==============================
add_executable(wasm_sqlite_db
    wasm_sqlite_db.c
)

target_link_libraries(wasm_sqlite_db PRIVATE
    wasi_socket
    PkgConfig::SQLITE3
)

# ==============================
# Edge Cache Server
# ==============================
add_executable(wasm_sqlite_cache
    wasm_sqlite_cache.c
)

target_link_libraries(wasm_sqlite_cache PRIVATE
    wasi_socket
    PkgConfig::SQLITE3
)
# ==============================
# Edge Cache Server Kubernetes
# ==============================
add_executable(wasm_sqlite_cache_kube
    wasm_sqlite_cache_kube.c
)

target_link_libraries(wasm_sqlite_cache_kube PRIVATE
    wasi_socket
    PkgConfig::SQLITE3
)

# ==============================
# テスト用サーバ
# ==============================
add_executable(wasm_sqlite_server
    test_wasm_sqlite_server.c
)

target_link_libraries(wasm_sqlite_server PRIVATE
    wasi_socket
    PkgConfig::SQLITE3
)