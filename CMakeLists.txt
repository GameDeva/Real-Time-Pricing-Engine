cmake_minimum_required(VERSION 3.14)
project(QuantPricingEngine VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ── Compiler flags ─────────────────────────────────────────────────────────────
# CMake already populates CMAKE_CXX_FLAGS_RELEASE (/O2) and CMAKE_CXX_FLAGS_DEBUG
# (/Od /RTC1) for MSVC, and -O2 / -O0 -g for GCC/Clang. We only add what is not
# provided by default: stricter warnings and conformance flags.
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
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

FetchContent_Declare(
    pybind11
    GIT_REPOSITORY https://github.com/pybind/pybind11.git
    GIT_TAG        v2.12.0
)
FetchContent_MakeAvailable(pybind11)

# ── Core library ───────────────────────────────────────────────────────────────
add_library(quant_core STATIC
    src/MathUtils.cpp
    src/BlackScholesPricer.cpp
    src/MonteCarloPricer.cpp
)

target_include_directories(quant_core PUBLIC include)
target_compile_features(quant_core PUBLIC cxx_std_20)
set_target_properties(quant_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

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
