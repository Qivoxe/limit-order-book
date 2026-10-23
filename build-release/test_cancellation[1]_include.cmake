if(EXISTS "/home/devshiv/projects/limit-order-book/build-release/test_cancellation")
  if(NOT EXISTS "/home/devshiv/projects/limit-order-book/build-release/test_cancellation[1]_tests.cmake" OR
     NOT "/home/devshiv/projects/limit-order-book/build-release/test_cancellation[1]_tests.cmake" IS_NEWER_THAN "/home/devshiv/projects/limit-order-book/build-release/test_cancellation" OR
     NOT "/home/devshiv/projects/limit-order-book/build-release/test_cancellation[1]_tests.cmake" IS_NEWER_THAN "${CMAKE_CURRENT_LIST_FILE}")
    include("/usr/share/cmake-4.2/Modules/GoogleTestAddTests.cmake")
    gtest_discover_tests_impl(
      TEST_EXECUTABLE [==[/home/devshiv/projects/limit-order-book/build-release/test_cancellation]==]
      TEST_EXECUTOR [==[]==]
      TEST_WORKING_DIR [==[/home/devshiv/projects/limit-order-book/build-release]==]
      TEST_EXTRA_ARGS [==[]==]
      TEST_PROPERTIES [==[]==]
      TEST_PREFIX [==[]==]
      TEST_SUFFIX [==[]==]
      TEST_FILTER [==[]==]
      NO_PRETTY_TYPES [==[FALSE]==]
      NO_PRETTY_VALUES [==[FALSE]==]
      TEST_LIST [==[test_cancellation_TESTS]==]
      CTEST_FILE [==[/home/devshiv/projects/limit-order-book/build-release/test_cancellation[1]_tests.cmake]==]
      TEST_DISCOVERY_TIMEOUT [==[5]==]
      TEST_DISCOVERY_EXTRA_ARGS [==[]==]
      TEST_XML_OUTPUT_DIR [==[]==]
    )
  endif()
  include("/home/devshiv/projects/limit-order-book/build-release/test_cancellation[1]_tests.cmake")
else()
  add_test(test_cancellation_NOT_BUILT test_cancellation_NOT_BUILT)
endif()
