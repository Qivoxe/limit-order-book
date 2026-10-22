
cmake_minimum_required(VERSION 3.20)

project(LimitOrderBook
    VERSION 0.1.0
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(lob INTERFACE)

target_include_directories(lob INTERFACE
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
