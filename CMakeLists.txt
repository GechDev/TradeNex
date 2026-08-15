cmake_minimum_required(VERSION 3.20)

project(TradeNex
    VERSION 1.0.0
    DESCRIPTION "High-performance C++20 limit order matching engine and market simulator"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(TRADENEX_BUILD_TESTS "Build automated test suite" ON)
option(TRADENEX_BUILD_BENCHMARKS "Build performance benchmark suite" ON)
option(TRADENEX_BUILD_APPS "Build CLI executable" ON)
option(TRADENEX_SANITIZE "Build with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(TRADENEX_NATIVE "Optimize for host CPU (-march=native)" OFF)
option(TRADENEX_USE_GTEST "Fetch and link external GoogleTest library" OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

add_library(tradenex_warnings INTERFACE)
if(MSVC)
    target_compile_options(tradenex_warnings INTERFACE /W4 /permissive- /Zc:__cplusplus)
else()
    target_compile_options(tradenex_warnings INTERFACE
        -Wall
        -Wextra
        -Wpedantic
        -Wshadow
        -Wnon-virtual-dtor
        -Wconversion
        -Wno-sign-conversion)
    if(TRADENEX_SANITIZE)
        target_compile_options(tradenex_warnings INTERFACE -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(tradenex_warnings INTERFACE -fsanitize=address,undefined)
    endif()
    if(TRADENEX_NATIVE)
        target_compile_options(tradenex_warnings INTERFACE -march=native)
    endif()
endif()

add_library(tradenex
    src/order_book.cpp
    src/matching_engine.cpp
    src/simulator.cpp
)
add_library(tradenex::tradenex ALIAS tradenex)

target_include_directories(tradenex
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)
target_link_libraries(tradenex PUBLIC tradenex_warnings)

if(TRADENEX_BUILD_APPS)
    add_executable(tradenex_cli src/main.cpp)
    target_link_libraries(tradenex_cli PRIVATE tradenex)
    set_target_properties(tradenex_cli PROPERTIES OUTPUT_NAME tradenex)
endif()

if(TRADENEX_BUILD_TESTS)
    enable_testing()

    if(TRADENEX_USE_GTEST)
        include(FetchContent)
        FetchContent_Declare(
            googletest
            URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        )
        set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
        FetchContent_MakeAvailable(googletest)
    endif()

    add_executable(tradenex_tests
        tests/order_book_tests.cpp
        tests/matching_tests.cpp
        tests/cancellation_tests.cpp
        tests/invariant_tests.cpp
        tests/replay_tests.cpp
    )
    target_link_libraries(tradenex_tests PRIVATE tradenex)
    target_include_directories(tradenex_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)

    if(TRADENEX_USE_GTEST)
        target_compile_definitions(tradenex_tests PRIVATE TRADENEX_USE_GTEST)
        target_link_libraries(tradenex_tests PRIVATE GTest::gtest_main)
    endif()

    add_test(NAME tradenex_tests COMMAND tradenex_tests)
endif()

if(TRADENEX_BUILD_BENCHMARKS)
    add_executable(tradenex_benchmark benchmarks/matching_benchmark.cpp)
    target_link_libraries(tradenex_benchmark PRIVATE tradenex)
endif()
