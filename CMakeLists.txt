#-------------------------------------------------------------------------------------------
# Copyright (c) 2020, OLogN Technologies AG
#-------------------------------------------------------------------------------------------
cmake_minimum_required(VERSION 3.12.4)
project(iibmalloc CXX)

include(CTest)

set(CMAKE_CXX_STANDARD 20)

set (CMAKE_MODULE_PATH "${CMAKE_MODULE_PATH};${CMAKE_CURRENT_SOURCE_DIR}/src/foundation/tools")
include(CommonCppFlags)


#-------------------------------------------------------------------------------------------
# Library definition
#-------------------------------------------------------------------------------------------
add_library(iibmalloc STATIC
  src/iibmalloc.cpp
  )

target_include_directories(iibmalloc
  PUBLIC include
  PUBLIC src
  )

add_subdirectory(src/foundation)

target_link_libraries(iibmalloc foundation)


#-------------------------------------------------------------------------------------------
# Tests 
#-------------------------------------------------------------------------------------------
#include(CMakePrintHelpers)

if (CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
  set(IIBMALLOC_TEST ON CACHE BOOL "Enable iibmalloc tests")
else()
  set(IIBMALLOC_TEST OFF CACHE BOOL "Enable iibmalloc tests")
endif()

if (IIBMALLOC_TEST)
  add_executable(test_iibmalloc
    test/test_common.cpp
    test/random_test.cpp
    )

  target_link_libraries(test_iibmalloc iibmalloc)

  add_test(Run_test_iibmalloc test_iibmalloc)
endif()
