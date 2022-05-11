cmake_minimum_required (VERSION 3.14)
cmake_policy(SET CMP0028 NEW)

project (ericw-tools)

set(CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake" ${CMAKE_MODULE_PATH})

# Grab the git describe output and store it in GIT_DESCRIBE
# Thanks to http://xit0.org/2013/04/cmake-use-git-branch-and-commit-details-in-project/
execute_process(
  COMMAND git describe --dirty
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  OUTPUT_VARIABLE GIT_DESCRIBE
  OUTPUT_STRIP_TRAILING_WHITESPACE
)

if (CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND CMAKE_CXX_SIMULATE_ID STREQUAL "MSVC")
	add_compile_options(/EHsc)
	message(STATUS "working around exceptions not being enabled by default on clang-cl")
endif()

include_directories(
	"${CMAKE_SOURCE_DIR}/include"
	"${CMAKE_SOURCE_DIR}/3rdparty/glm")

find_package (Threads)

if (CMAKE_USE_PTHREADS_INIT)
	add_definitions(-DUSE_PTHREADS)
elseif (CMAKE_USE_WIN32_THREADS_INIT)
	add_definitions(-DUSE_WIN32THREADS)
endif ()

if (UNIX)
	add_definitions(-DLINUX)
endif (UNIX)

# set our C/C++ dialects
if (CMAKE_VERSION VERSION_LESS "3.1")
	set (CMAKE_CXX_FLAGS "-std=gnu++11 ${CMAKE_CXX_FLAGS}")
	set (CMAKE_C_FLAGS "-std=gnu11 ${CMAKE_C_FLAGS}")
else ()
	set(CMAKE_CXX_STANDARD 17)
	set(CMAKE_CXX_STANDARD_REQUIRED ON)
	set(CMAKE_C_STANDARD 99)
endif ()

add_definitions(-DERICWTOOLS_VERSION=${GIT_DESCRIBE})

if (WIN32)
	set("NO_ITERATOR_DEBUG" FALSE CACHE BOOL "Whether to use MSVC iterator debugging or not")

	if (NO_ITERATOR_DEBUG)
		add_definitions(-D_ITERATOR_DEBUG_LEVEL=0)
		add_definitions(-D_CONTAINER_DEBUG_LEVEL=0)
	endif (NO_ITERATOR_DEBUG)

	# TODO: remove these 
	add_compile_definitions(_CRT_SECURE_NO_WARNINGS)

	if (MSVC)
		# request 8MB stack for all .exe's
		set(STACKSIZE 8388608)
		set(CMAKE_EXE_LINKER_FLAGS "/STACK:${STACKSIZE}")

		add_definitions("/wd4244") # disable "conversion from .. to .., possible loss of data" warning
		add_definitions("/wd4018") # disable "signed/unsigned mismatch" warning
		add_definitions("/wd4200") # disable "nonstandard extension used: zero-sized array in struct/union" warning
		add_definitions("/wd4264") # disable "conversion from 'size_t' to 'int', possible loss of data" warning
		add_definitions("/wd4267") # disable "conversion from 'size_t' to 'int', possible loss of data" warning
		add_definitions("/wd4305") # disable "truncation from 'double' to 'float'" warning
		add_definitions("/wd4250") # disable "inherits via dominance' warning (used in trace_embree.cc)
	endif (MSVC)
endif (WIN32)

# Pass -DERICWTOOLS_ASAN=YES to enable for all targets
if (ERICWTOOLS_ASAN)
	message(STATUS "Enabling ASan on all targets")
	add_compile_options(-fsanitize=address)
	add_link_options(-fsanitize=address)
endif()

# 10.9: minimum version that supports unordered_map
# 10.14: required by tbb 2021.3.0 (due to use of alignas)
# 10.15: required by std::filesytstem
set(CMAKE_OSX_DEPLOYMENT_TARGET 10.15)

find_package(TBB REQUIRED)

add_subdirectory(3rdparty)
add_subdirectory(common)
add_subdirectory(bspinfo)
add_subdirectory(bsputil)
add_subdirectory(light)

if (ENABLE_LIGHTPREVIEW)
	add_subdirectory(lightpreview)
endif ()

add_subdirectory(qbsp)
add_subdirectory(vis)
add_subdirectory(docs)

install(FILES README.md DESTINATION bin)
install(FILES changelog.md DESTINATION bin)

#CPack configuration

set(CPACK_GENERATOR ZIP)
set(CPACK_PACKAGE_NAME ericw-tools)
set(CPACK_PACKAGE_VERSION ${GIT_DESCRIBE})
include(CPack)
