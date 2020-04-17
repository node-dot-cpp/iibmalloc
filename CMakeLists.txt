#-------------------------------------------------------------------------------------------
# Copyright (c) 2020, OLogN Technologies AG
#-------------------------------------------------------------------------------------------
cmake_minimum_required(VERSION 3.0)
project(iibmalloc CXX)
set(CMAKE_CXX_STANDARD 17)
include(CTest)

#-------------------------------------------------------------------------------------------
# Defines
#-------------------------------------------------------------------------------------------

if(MSVC)
	add_compile_options(/W3)
	add_compile_options(/EHa)
#	add_compile_options(/MD)
else()
	add_compile_options( -Wall )
	add_compile_options( -Wextra )
	add_compile_options( -Wno-unused-variable )
	add_compile_options( -Wno-unused-parameter )
	add_compile_options( -Wno-empty-body )
	add_compile_options(-fexceptions)
	add_compile_options(-lto )
	add_compile_options(-fnon-call-exceptions)
#	add_compile_options(-lpthread)
	add_compile_options(-O2)
endif()

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
