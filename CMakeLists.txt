cmake_minimum_required(VERSION 3.13)
project(StrideUtils)

set(STRIDEUTILS_SRC
  src/astfunctions.cpp
  src/astquery.cpp
  src/astruntime.cpp
  src/astvalidation.cpp
  src/stridelibrary.cpp
)

set(STRIDEUTILS_HEADERS
  include/stride/utils/astfunctions.h
  include/stride/utils/astquery.h
  include/stride/utils/astruntime.h
  include/stride/utils/astvalidation.h
  include/stride/utils/stridelibrary.h
)

add_library(StrideUtils ${STRIDEUTILS_SRC} ${STRIDEUTILS_HEADERS})

set_property(TARGET StrideUtils PROPERTY POSITION_INDEPENDENT_CODE ON)
set_property(TARGET StrideUtils PROPERTY CXX_STANDARD 17)
set_property(TARGET StrideUtils PROPERTY CXX_STANDARD_REQUIRED ON)

target_include_directories(StrideUtils PUBLIC "${CMAKE_CURRENT_LIST_DIR}/include")

target_link_libraries(StrideUtils PUBLIC StrideParser)

add_subdirectory(tests)
