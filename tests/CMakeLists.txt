cmake_minimum_required(VERSION 3.13)

set(STRIDEUTILS_TEST_SRC
  library.cpp
  # ast_utils_test.cpp
)

add_executable(StrideUtilsTests main.cpp schema.cpp library.cpp ${STRIDEUTILS_TEST_SRC})

target_link_libraries(StrideUtilsTests PRIVATE StrideUtils GTest::gtest_main)

target_compile_definitions(StrideUtilsTests PRIVATE 
    TESTS_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data/"
)

add_test(NAME StrideUtilsTests COMMAND StrideUtilsTests)
