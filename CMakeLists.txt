cmake_minimum_required(VERSION 3.14)
project(QuantPricingEngine VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ── Compiler flags ─────────────────────────────────────────────────────────────
if(MSVC)
    add_compile_options(/W4 /permissive-)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
    add_compile_options($<$<CONFIG:Debug>:-fsanitize=address,undefined>)
    add_link_options($<$<CONFIG:Debug>:-fsanitize=address,undefined>)
endif()

# ── Dependencies via FetchContent ──────────────────────────────────────────────
include(FetchContent)

FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.14.0
    GIT_SHALLOW    ON
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

FetchContent_Declare(
    pybind11
    GIT_REPOSITORY https://github.com/pybind/pybind11.git
    GIT_TAG        v2.12.0
    GIT_SHALLOW    ON
)
FetchContent_MakeAvailable(pybind11)

# ── nlohmann/json ──────────────────────────────────────────────────────────────
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
    GIT_SHALLOW    ON
)
FetchContent_MakeAvailable(nlohmann_json)

# ── ixwebsocket ────────────────────────────────────────────────────────────────
# Force OpenSSL backend (mbedTLS not typically present on Windows or Docker slim).
# On Windows: pre-installed at C:/Program Files/OpenSSL-Win64 (Git for Windows).
# On Linux/Docker: libssl-dev installed in the builder layer — no hint needed.
# Zlib/deflate compression is disabled: Binance depth@100ms uses plain JSON.
set(USE_TLS 1 CACHE BOOL "Enable TLS/wss:// support in ixwebsocket" FORCE)
set(USE_OPEN_SSL 1 CACHE BOOL "Use OpenSSL (not mbedTLS)" FORCE)
set(USE_ZLIB 0 CACHE BOOL "Disable zlib (no per-message deflate needed)" FORCE)
if(WIN32)
    # Point find_package(OpenSSL) at the standard Git-for-Windows install location.
    if(NOT DEFINED OPENSSL_ROOT_DIR)
        set(OPENSSL_ROOT_DIR "C:/Program Files/OpenSSL-Win64"
            CACHE PATH "OpenSSL root directory on Windows" FORCE)
    endif()
endif()
FetchContent_Declare(
    ixwebsocket
    GIT_REPOSITORY https://github.com/machinezone/IXWebSocket.git
    GIT_TAG        v11.4.6
    GIT_SHALLOW    ON
)
FetchContent_MakeAvailable(ixwebsocket)

# ── Core library ───────────────────────────────────────────────────────────────
add_library(quant_core STATIC
    src/MathUtils.cpp
    src/BlackScholesPricer.cpp
    src/MonteCarloPricer.cpp
    src/market_data/OrderBook.cpp
    src/market_data/MarketDataClient.cpp
    src/market_data/LivePricer.cpp
)

target_include_directories(quant_core PUBLIC include)
target_compile_features(quant_core PUBLIC cxx_std_20)
set_target_properties(quant_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_link_libraries(quant_core PUBLIC
    nlohmann_json::nlohmann_json
    ixwebsocket
)

# ── Python binding module ──────────────────────────────────────────────────────
pybind11_add_module(quant_pricer bindings/PybindModule.cpp)
target_link_libraries(quant_pricer PRIVATE quant_core)

set_target_properties(quant_pricer PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY         "${CMAKE_SOURCE_DIR}/python_app"
    RUNTIME_OUTPUT_DIRECTORY         "${CMAKE_SOURCE_DIR}/python_app"
    LIBRARY_OUTPUT_DIRECTORY_RELEASE "${CMAKE_SOURCE_DIR}/python_app"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_SOURCE_DIR}/python_app"
    LIBRARY_OUTPUT_DIRECTORY_DEBUG   "${CMAKE_SOURCE_DIR}/python_app"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG   "${CMAKE_SOURCE_DIR}/python_app"
)

# ── Tests ──────────────────────────────────────────────────────────────────────
enable_testing()
add_subdirectory(tests)
