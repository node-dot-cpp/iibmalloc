cmake_minimum_required(VERSION 3.0)
project(iibmalloc)

find_package(Threads)

set(LIB_NAME iibmalloc)
set(LIB_D_NAME iibmalloc_d)
set(TEST_BIN_NAME test_iibmalloc.bin)
set(TEST_D_BIN_NAME test_iibmalloc_d.bin)

add_subdirectory(${PROJECT_SOURCE_DIR}/src/foundation build)

if(MSVC OR MSYS OR MINGW)
	option(CMAKE_USE_WIN32_THREADS_INIT "using WIN32 threads" ON)
	set(SOURCE_LIB src/page_allocator_windows.cpp src/iibmalloc_windows.cpp )
	add_definitions(/W3)
	add_definitions(/EHa)
else()
	enable_testing()
	set(SOURCE_LIB src/page_allocator_linux.cpp src/iibmalloc_linux.cpp)
	add_definitions( -Wall )
	add_definitions( -Wextra )
	add_definitions( -Wno-unused-variable )
	add_definitions( -Wno-unused-parameter )
	add_definitions( -Wno-empty-body )
	add_definitions(-fexceptions)
	add_definitions(-lto )
	add_definitions(-fnon-call-exceptions)
	add_definitions(-lpthread)
	add_compile_options(-O2)
endif()


include_directories(include  ${PROJECT_SOURCE_DIR}/src/foundation/3rdparty/fmt/include
							 ${PROJECT_SOURCE_DIR}/src/foundation/include
							 ${PROJECT_SOURCE_DIR}/include
							 ${PROJECT_SOURCE_DIR}/src	)

set (SOURCE_EXE  test/test_common.cpp test/random_test.cpp  )
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED on)
set(CMAKE_CXX_FLAGS_DEBUG " ${CMAKE_CXX_FLAGS_DEBUG} -DDEBUG")
set(CMAKE_CXX_FLAGS_RELEASE " ${CMAKE_CXX_FLAGS_RELEASE}  -DNDEBUG")
#set(CMAKE_CXX_FLAGS  " ${CMAKE_CXX_FLAGS} -O2 " )

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/build/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/build/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/build/bin)

add_library( ${LIB_NAME} STATIC ${SOURCE_LIB})
add_library(${LIB_D_NAME} STATIC ${SOURCE_LIB})

target_link_libraries( ${LIB_NAME} foundation )
target_link_libraries( ${LIB_NAME} foundation_d)

add_executable(${TEST_BIN_NAME} EXCLUDE_FROM_ALL ${SOURCE_EXE})
add_executable(${TEST_D_BIN_NAME} EXCLUDE_FROM_ALL ${SOURCE_EXE})

set_target_properties ( ${LIB_NAME} PROPERTIES COMPILE_FLAGS  ${CMAKE_CXX_FLAGS_RELEASE})
set_target_properties ( ${LIB_D_NAME} PROPERTIES COMPILE_FLAGS  ${CMAKE_CXX_FLAGS_DEBUG} )

set_target_properties ( ${TEST_BIN_NAME} PROPERTIES COMPILE_FLAGS  ${CMAKE_CXX_FLAGS_RELEASE})
set_target_properties ( ${TEST_D_BIN_NAME}  PROPERTIES COMPILE_FLAGS  ${CMAKE_CXX_FLAGS_DEBUG})

target_compile_options(${TEST_BIN_NAME} PRIVATE -pthread)
target_link_libraries(${TEST_BIN_NAME} ${LIB_NAME})

target_compile_options(${TEST_D_BIN_NAME} PRIVATE -pthread)
target_link_libraries(${TEST_D_BIN_NAME} ${LIB_NAME})

if (UNIX)
	target_link_libraries(${TEST_BIN_NAME}  pthread )
	target_link_libraries(${TEST_D_BIN_NAME} pthread )
endif (UNIX)

if(UNIX)
	add_test( Build_iibmalloc_test  make ${TEST_BIN_NAME} )
	add_test( Build_iibmalloc_test_d  make ${TEST_D_BIN_NAME} )
	add_test( Run_iibmalloc_test "${PROJECT_SOURCE_DIR}/build/bin/${TEST_BIN_NAME}" )
	add_test( Rub_iibmalloc_test_d "${PROJECT_SOURCE_DIR}/build/bin/${TEST_D_BIN_NAME}" )
endif(UNIX)