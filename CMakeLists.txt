
cmake_minimum_required(VERSION 3.20)

project(LimitOrderBook
    VERSION 0.1.0
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(lob
    src/order_book.cpp
)

target_include_directories(lob PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

add_executable(lob_engine src/main.cpp)
target_link_libraries(lob_engine PRIVATE lob)

enable_testing()
find_package(GTest REQUIRED)

add_executable(test_order
    tests/test_order.cpp
)

target_link_libraries(test_order PRIVATE
    lob
    GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(test_order)
add_executable(test_order_book
    tests/test_order_book.cpp
)

target_link_libraries(test_order_book PRIVATE
    lob
    GTest::gtest_main
)

gtest_discover_tests(test_order_book)

add_executable(test_matching
    tests/test_matching.cpp
)

target_link_libraries(test_matching PRIVATE
    lob
    GTest::gtest_main
)

gtest_discover_tests(test_matching)
add_executable(test_cancellation
    tests/test_cancellation.cpp
)

target_link_libraries(test_cancellation PRIVATE
    lob
    GTest::gtest_main
)

gtest_discover_tests(test_cancellation)
add_executable(test_invariants
    tests/test_invariants.cpp
)

target_link_libraries(test_invariants PRIVATE
    lob
    GTest::gtest_main
)

gtest_discover_tests(test_invariants)
add_executable(lob_benchmark
    benchmarks/benchmark.cpp
)

target_link_libraries(lob_benchmark PRIVATE lob)
add_executable(cancellation_benchmark
    benchmarks/cancellation_benchmark.cpp
)

target_link_libraries(cancellation_benchmark PRIVATE lob)
