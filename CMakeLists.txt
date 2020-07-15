#-------------------------------------------------------------------------------------------
# Copyright (c) 2020, OLogN Technologies AG
#-------------------------------------------------------------------------------------------
cmake_minimum_required(VERSION 3.0)
project(iibmalloc CXX)

include(CTest)

set(CMAKE_CXX_STANDARD 17)

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

if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
	# we need async exceptions
	string( REPLACE "/EHsc" "/EHa" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}" )
endif()

#-------------------------------------------------------------------------------------------
# Tests 
#-------------------------------------------------------------------------------------------
add_executable(test_iibmalloc
  test/test_common.cpp
  test/random_test.cpp
  )

target_link_libraries(test_iibmalloc iibmalloc)

add_test(Run_test_iibmalloc test_iibmalloc)
