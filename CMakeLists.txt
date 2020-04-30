#-------------------------------------------------------------------------------------------
# Copyright (c) 2020, OLogN Technologies AG
#-------------------------------------------------------------------------------------------
cmake_minimum_required(VERSION 3.0)
project(iibmalloc CXX)
set(CMAKE_CXX_STANDARD 17)
include(CTest)

set (CMAKE_MODULE_PATH 
  "${CMAKE_MODULE_PATH};${CMAKE_CURRENT_SOURCE_DIR}/src/foundation/tools")
include(CommonCppFlags)


#-------------------------------------------------------------------------------------------
# Library definition
#-------------------------------------------------------------------------------------------
add_library(iibmalloc STATIC
  src/iibmalloc.cpp
  )

#-------------------------------------------------------------------------------------------
# Export Include Directories
#-------------------------------------------------------------------------------------------
target_include_directories(iibmalloc
  PUBLIC include
  PUBLIC src
  )

#-------------------------------------------------------------------------------------------
# Package Dependencies 
#-------------------------------------------------------------------------------------------
add_subdirectory(src/foundation)

target_link_libraries(iibmalloc foundation)

#-------------------------------------------------------------------------------------------
# Tests 
#-------------------------------------------------------------------------------------------
add_executable(test_iibmalloc
  test/test_common.cpp
  test/random_test.cpp
  )

target_link_libraries(test_iibmalloc iibmalloc)

add_test(Run_test_iibmalloc test_iibmalloc)
