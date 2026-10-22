cmake_minimum_required(VERSION 3.20)

project(LimitOrderBook
    VERSION 0.1.0
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(LOB_ENABLE_WARNINGS "Enable common compiler warnings" ON)
option(LOB_BUILD_DEMO "Build the exchange simulator demonstration" ON)
option(LOB_BUILD_BENCHMARKS "Build synthetic benchmark programs" ON)

include(CTest)

add_library(lob src/order_book.cpp)
target_include_directories(lob PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

if(LOB_ENABLE_WARNINGS)
    if(MSVC)
        target_compile_options(lob PRIVATE /W4)
    else()
        target_compile_options(lob PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endif()

if(LOB_BUILD_DEMO)
    add_executable(lob_engine src/main.cpp)
    target_link_libraries(lob_engine PRIVATE lob)
endif()

if(BUILD_TESTING)
    find_package(GTest REQUIRED)
    include(GoogleTest)

    foreach(test_suite IN ITEMS
        order
        order_book
        matching
        cancellation
        invariants
        exchange_simulator
    )
        add_executable(test_${test_suite} tests/test_${test_suite}.cpp)
        target_link_libraries(test_${test_suite} PRIVATE lob GTest::gtest_main)
        gtest_discover_tests(test_${test_suite} DISCOVERY_MODE PRE_TEST)
    endforeach()
endif()

if(LOB_BUILD_BENCHMARKS)
    add_executable(lob_benchmark benchmarks/benchmark.cpp)
    target_link_libraries(lob_benchmark PRIVATE lob)
    add_executable(cancellation_benchmark benchmarks/cancellation_benchmark.cpp)
    target_link_libraries(cancellation_benchmark PRIVATE lob)
endif()
