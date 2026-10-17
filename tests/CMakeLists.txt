cmake_minimum_required(VERSION 3.13)

set(STRIDEUTILS_TEST_SRC
  library.cpp
  schema.cpp
  astquery_tests.cpp
  astfunctions_tests.cpp
  astquery_statemachine_test.cpp
  logger_tests.cpp
  cache_tests.cpp
)

add_executable(strideutils_tests main.cpp ${STRIDEUTILS_TEST_SRC})

set_property(TARGET strideutils_tests PROPERTY CXX_STANDARD 20)
set_property(TARGET strideutils_tests PROPERTY CXX_STANDARD_REQUIRED ON)

target_link_libraries(strideutils_tests PRIVATE StrideUtils GTest::gtest_main)

target_compile_definitions(strideutils_tests PRIVATE
    TESTS_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data/"
)

add_test(NAME strideutils_tests COMMAND strideutils_tests)
