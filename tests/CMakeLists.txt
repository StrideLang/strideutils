cmake_minimum_required(VERSION 3.13)

set(STRIDEUTILS_TEST_SRC
  library.cpp
  schema.cpp
)

add_executable(strideutils_tests main.cpp ${STRIDEUTILS_TEST_SRC})

target_link_libraries(strideutils_tests PRIVATE StrideUtils GTest::gtest_main)

target_compile_definitions(strideutils_tests PRIVATE
    TESTS_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data/"
)

add_test(NAME strideutils_tests COMMAND StrideUtilsTests)
