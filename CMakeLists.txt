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
option(BUILD_TESTS       "Build unit, integration and performance tests" ON)
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
# MSVC-specific flags
# ============================================================================
if(MSVC)
    add_compile_options(/EHsc /W3 /permissive- /Zc:__cplusplus /utf-8)
    add_compile_definitions(
        _USE_MATH_DEFINES
        _CRT_SECURE_NO_WARNINGS
        _SILENCE_CXX23_DENORM_DEPRECATION_WARNING
        NOMINMAX
        WIN32_LEAN_AND_MEAN
    )
endif()

# ============================================================================
# FetchContent dependencies
# ============================================================================
include(FetchContent)

# ---- Eigen3 (header-only) -------------------------------------------------
set(EIGEN_BUILD_DOC       OFF CACHE BOOL "" FORCE)
set(EIGEN_BUILD_TESTING   OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING         OFF CACHE BOOL "" FORCE)
set(EIGEN_TEST_CXX11      OFF CACHE BOOL "" FORCE)
FetchContent_Declare(eigen
    GIT_REPOSITORY https://gitlab.com/libeigen/eigen.git
    GIT_TAG 5.0.1
)
FetchContent_MakeAvailable(eigen)
if(NOT TARGET Eigen3::Eigen)
    add_library(Eigen3::Eigen INTERFACE)
    target_include_directories(Eigen3::Eigen INTERFACE ${eigen_SOURCE_DIR})
endif()

# ---- fmt (external) -------------------------------------------------------
FetchContent_Declare(fmt
    GIT_REPOSITORY https://github.com/fmtlib/fmt.git
    GIT_TAG 11.0.2
)
FetchContent_MakeAvailable(fmt)

# ---- spdlog (uses external fmt) -------------------------------------------
set(SPDLOG_FMT_EXTERNAL ON CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_TESTS  OFF CACHE BOOL "" FORCE)
FetchContent_Declare(spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG v1.15.0
)
FetchContent_MakeAvailable(spdlog)

# ---- Zstd (static) --------------------------------------------------------
set(ZSTD_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
set(ZSTD_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(ZSTD_BUILD_SHARED   OFF CACHE BOOL "" FORCE)
set(ZSTD_BUILD_STATIC   ON  CACHE BOOL "" FORCE)
FetchContent_Declare(zstd
    GIT_REPOSITORY https://github.com/facebook/zstd.git
    GIT_TAG v1.5.6
)
FetchContent_MakeAvailable(zstd)

set(ZSTD_LINK_TARGET "")
if(TARGET libzstd_static)
    set(ZSTD_LINK_TARGET libzstd_static)
elseif(TARGET zstd_static)
    set(ZSTD_LINK_TARGET zstd_static)
elseif(TARGET zstd)
    set(ZSTD_LINK_TARGET zstd)
endif()

# ---- prometheus-cpp (with ENABLE_PULL for Exposer) ------------------------
set(ENABLE_PULL        ON  CACHE BOOL "" FORCE)
set(ENABLE_PUSH        OFF CACHE BOOL "" FORCE)
set(ENABLE_COMPRESSION OFF CACHE BOOL "" FORCE)
set(PROMETHEUS_USE_THIRDPARTY_LIBS ON CACHE BOOL "" FORCE)
FetchContent_Declare(prometheus-cpp
    GIT_REPOSITORY https://github.com/jupp0r/prometheus-cpp.git
    GIT_TAG v1.2.4
)
FetchContent_MakeAvailable(prometheus-cpp)

# ---- cpp-httplib (health endpoint) ----------------------------------------
set(HTTPLIB_REQUIRE_OPENSSL ON CACHE BOOL "" FORCE)
set(HTTPLIB_USE_OPENSSL_IF_AVAILABLE ON CACHE BOOL "" FORCE)
FetchContent_Declare(httplib
    GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
    GIT_TAG v0.18.3
)
FetchContent_MakeAvailable(httplib)

# ---- Protobuf (installed via vcpkg) ---------------------------------------
find_package(Protobuf CONFIG REQUIRED)

# ============================================================================
# System libraries
# ============================================================================

# OpenSSL (native install)
if(WIN32)
    set(OPENSSL_ROOT_DIR "C:/Program Files/OpenSSL-Win64" CACHE PATH "" FORCE)
    set(OPENSSL_USE_STATIC_LIBS FALSE)
endif()
find_package(OpenSSL REQUIRED)

# Threads
find_package(Threads REQUIRED)

# PostgreSQL / libpq (native install at C:\Program Files\PostgreSQL\18)
if(WIN32)
    set(PostgreSQL_ROOT "C:/Program Files/PostgreSQL/18" CACHE PATH "" FORCE)
    set(PostgreSQL_INCLUDE_DIR "C:/Program Files/PostgreSQL/18/include" CACHE PATH "" FORCE)
    set(PostgreSQL_LIBRARY "C:/Program Files/PostgreSQL/18/lib/libpq.lib" CACHE FILEPATH "" FORCE)
    set(PostgreSQL_TYPE_INCLUDE_DIR "C:/Program Files/PostgreSQL/18/include" CACHE PATH "" FORCE)
endif()
find_package(PostgreSQL REQUIRED)

# DCMTK (installed via vcpkg)
find_package(DCMTK CONFIG REQUIRED)

# librdkafka (installed via vcpkg)
find_path(RDKAFKA_INCLUDE_DIR
    NAMES librdkafka/rdkafka.h
    HINTS "${CMAKE_PREFIX_PATH}/include"
    REQUIRED
)
find_library(RDKAFKA_LIBRARY
    NAMES rdkafka
    HINTS "${CMAKE_PREFIX_PATH}/lib"
    REQUIRED
)

# ============================================================================
# Source files
# ============================================================================
set(ROUTER_SOURCES
    main.cpp

    # Domain (header-only)
    domain/value_objects/dimensionality.hpp
    domain/value_objects/study_uid.hpp
    domain/entities/routing_request.hpp
    domain/services/dimensionality_detector.hpp
    domain/events/routing_event.hpp
    domain/exceptions/routing_exception.hpp
    domain/repositories/i_routing_repository.hpp

    # Application
    application/ports/inbound/i_route_request_use_case.hpp
    application/ports/outbound/i_message_publisher.hpp
    application/ports/outbound/i_dicom_reader.hpp
    application/dtos/routing_dto.hpp
    application/use_cases/route_request_use_case.hpp
    application/use_cases/route_request_use_case.cpp

    # Infrastructure
    infrastructure/adapters/config/config_loader.hpp
    infrastructure/adapters/config/config_loader.cpp
    infrastructure/adapters/logging/logger.hpp
    infrastructure/adapters/logging/logger.cpp
    infrastructure/adapters/security/tls_context.hpp
    infrastructure/adapters/security/tls_context.cpp
    infrastructure/adapters/kafka/kafka_producer_adapter.hpp
    infrastructure/adapters/kafka/kafka_producer_adapter.cpp
    infrastructure/adapters/kafka/kafka_consumer_adapter.hpp
    infrastructure/adapters/kafka/kafka_consumer_adapter.cpp
    infrastructure/adapters/postgres/postgres_repository_adapter.hpp
    infrastructure/adapters/postgres/postgres_repository_adapter.cpp
    infrastructure/adapters/dicom/dicom_reader_adapter.hpp
    infrastructure/adapters/dicom/dicom_reader_adapter.cpp
    infrastructure/adapters/metrics/metrics_server.hpp
    infrastructure/adapters/metrics/metrics_server.cpp
    infrastructure/adapters/http/health_endpoint.hpp
    infrastructure/adapters/http/health_endpoint.cpp
)

# ============================================================================
# Executable
# ============================================================================
add_executable(router ${ROUTER_SOURCES})

target_compile_features(router PRIVATE cxx_std_23)

# ============================================================================
# Include directories
# ============================================================================
target_include_directories(router PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${eigen_SOURCE_DIR}
    ${RDKAFKA_INCLUDE_DIR}
)

# ============================================================================
# Link libraries
# ============================================================================
target_link_libraries(router PRIVATE
    Eigen3::Eigen
    spdlog::spdlog
    fmt::fmt
    prometheus-cpp::core
    prometheus-cpp::pull
    httplib::httplib
    protobuf::libprotobuf
    OpenSSL::SSL
    OpenSSL::Crypto
    Threads::Threads
    PostgreSQL::PostgreSQL
    DCMTK::DCMTK
    ${RDKAFKA_LIBRARY}
)

if(ZSTD_LINK_TARGET)
    target_link_libraries(router PRIVATE ${ZSTD_LINK_TARGET})
endif()

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
if(BUILD_TESTS AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt")
    enable_testing()
    add_subdirectory(tests)
endif()