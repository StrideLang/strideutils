cmake_minimum_required(VERSION 3.13)
project(StrideUtils)

set(STRIDEUTILS_SRC
  src/astfunctions.cpp
  src/astquery.cpp
  src/astruntime.cpp
  src/astvalidation.cpp
  src/stridelibrary.cpp
  src/logger.cpp
  src/zpp_ast_serializer.cpp
  src/cachemanager.cpp
)

set(STRIDEUTILS_HEADERS
  include/stride/utils/astfunctions.h
  include/stride/utils/astquery.h
  include/stride/utils/astruntime.h
  include/stride/utils/astvalidation.h
  include/stride/utils/stridelibrary.h
  include/stride/utils/logger.h
  include/stride/utils/iastserializer.h
  include/stride/utils/cacheenvelope.h
  include/stride/utils/cachemanager.h
)

add_library(StrideUtils ${STRIDEUTILS_SRC} ${STRIDEUTILS_HEADERS})

set_property(TARGET StrideUtils PROPERTY POSITION_INDEPENDENT_CODE ON)
set_property(TARGET StrideUtils PROPERTY CXX_STANDARD 20)
set_property(TARGET StrideUtils PROPERTY CXX_STANDARD_REQUIRED ON)

target_include_directories(StrideUtils PUBLIC "${CMAKE_CURRENT_LIST_DIR}/include")

target_link_libraries(StrideUtils PUBLIC StrideParser)

# Where strideroot is when STRIDEROOT is not set and none is installed next
# to the program or in the user's folder (see getDefaultStrideRoot)
set(STRIDEROOT_DEFAULT "" CACHE PATH "strideroot used when no other is found")
if(STRIDEROOT_DEFAULT)
  target_compile_definitions(StrideUtils PRIVATE
                             STRIDEROOT_DEFAULT="${STRIDEROOT_DEFAULT}")
endif()

# The compiler is identified to the cache by the commits of its sources, and
# is experimental (never uses the cache) while they have uncommitted changes.
# The sibling repositories default to the layout of the Stride submodules.
set(STRIDE_COMPILER_SOURCE_DIRS "" CACHE STRING
    "Git work trees of the compiler's sources, for the cache's compiler hash")
if(NOT STRIDE_COMPILER_SOURCE_DIRS)
  set(compilerSourceDirs "${CMAKE_CURRENT_SOURCE_DIR}")
  foreach(sibling strideparser stridegen stridejit)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../${sibling}/.git")
      get_filename_component(siblingDir
        "${CMAKE_CURRENT_SOURCE_DIR}/../${sibling}" ABSOLUTE)
      list(APPEND compilerSourceDirs "${siblingDir}")
    endif()
  endforeach()
else()
  set(compilerSourceDirs "${STRIDE_COMPILER_SOURCE_DIRS}")
endif()

set(compilerVersionHeader
  "${CMAKE_CURRENT_BINARY_DIR}/generated/stride/utils/compilerversion.h")
# '|' separates the directories: a ';' doesn't survive the command line
string(REPLACE ";" "|" compilerSourceDirsArg "${compilerSourceDirs}")
# Runs on every build: git status can change without CMake knowing
add_custom_target(StrideCompilerVersion
  COMMAND ${CMAKE_COMMAND}
    "-DSOURCE_DIRS=${compilerSourceDirsArg}"
    "-DOUTPUT=${compilerVersionHeader}"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/StrideCompilerVersion.cmake"
  BYPRODUCTS "${compilerVersionHeader}"
  COMMENT "Checking the Stride compiler sources for changes"
  VERBATIM
)
add_dependencies(StrideUtils StrideCompilerVersion)
target_include_directories(StrideUtils PRIVATE
  "${CMAKE_CURRENT_BINARY_DIR}/generated")

add_subdirectory(tests)
