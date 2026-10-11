cmake_minimum_required(VERSION 3.28)
project(router VERSION 1.0.0 LANGUAGES CXX)

# ============================================================================
# Global C++23 enforcement
# ============================================================================
set(CMAKE_CXX_STANDARD 23 CACHE STRING "Enforced C++ standard" FORCE)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_CXX_STANDARD EQUAL 23)
    message(FATAL_ERROR "C++23 is required. Current: ${CMAKE_CXX_STANDARD}")
endif()

# ============================================================================
# Options
# ============================================================================
option(BUILD_TESTS "Build unit, integration and performance tests" ON)
option(ENABLE_SANITIZERS "Enable ASan/UBSan in Debug builds" ON)
option(ENABLE_CLANG_TIDY "Enable clang-tidy static analysis" ON)

# ============================================================================
# Sanitizers (Debug only)
# ============================================================================
if(ENABLE_SANITIZERS AND CMAKE_BUILD_TYPE STREQUAL "Debug")
    if(MSVC)
        add_compile_options(/fsanitize=address)
        add_link_options(/fsanitize=address)
    else()
        add_compile_options(-fsanitize=address -fsanitize=undefined -fno-omit-frame-pointer)
        add_link_options(-fsanitize=address -fsanitize=undefined)
    endif()
endif()

# ============================================================================
# Dependencies (to be filled in later steps)
# ============================================================================
include(FetchContent)

# Placeholder: Kafka, Protobuf, spdlog, OpenSSL will be added in subsequent steps.

# ============================================================================
# Source files
# ============================================================================
file(GLOB_RECURSE ROUTER_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/domain/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/application/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/infrastructure/*.cpp"
)

add_executable(router ${ROUTER_SOURCES} main.cpp)

target_include_directories(router PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# ============================================================================
# Static analysis
# ============================================================================
if(ENABLE_CLANG_TIDY)
    find_program(CLANG_TIDY_EXE clang-tidy)
    if(CLANG_TIDY_EXE)
        set_target_properties(router PROPERTIES
            CXX_CLANG_TIDY "${CLANG_TIDY_EXE};-checks=*,-llvmlibc-*")
    endif()
endif()

# ============================================================================
# Tests
# ============================================================================
if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
