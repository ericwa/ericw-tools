cmake_minimum_required (VERSION 3.14)
cmake_policy(SET CMP0028 NEW)

project (ericw-tools)

list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")
list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/3rdparty/Catch2/extras")

# Grab the git describe output and store it in GIT_DESCRIBE
# Thanks to http://xit0.org/2013/04/cmake-use-git-branch-and-commit-details-in-project/
execute_process(
  COMMAND git describe --dirty
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  OUTPUT_VARIABLE GIT_DESCRIBE
  OUTPUT_STRIP_TRAILING_WHITESPACE
)
message(STATUS "git describe returned \"${GIT_DESCRIBE}\"")

# git describe fails on GitHub Actions
if(NOT GIT_DESCRIBE)
	# See: https://docs.github.com/en/actions/reference/environment-variables#default-environment-variables
	if(NOT ("$ENV{GITHUB_REF_NAME}" STREQUAL ""))
		set(GIT_DESCRIBE "$ENV{GITHUB_REF_NAME}")
		
		message(STATUS "using version label \"${GIT_DESCRIBE}\" from GITHUB_REF_NAME")
	endif()
endif()

if (CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND CMAKE_CXX_SIMULATE_ID STREQUAL "MSVC")
	add_compile_options(/EHsc)
	message(STATUS "working around exceptions not being enabled by default on clang-cl")
endif()

include_directories(include)

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
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 99)

set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_DEBUG OFF)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO OFF)

if (UNIX AND NOT APPLE)
	set(CMAKE_INSTALL_RPATH "$ORIGIN")
	set(CMAKE_BUILD_WITH_INSTALL_RPATH YES)
endif ()

add_definitions(-DERICWTOOLS_VERSION="${GIT_DESCRIBE}")

# MINGW stuff
if(MINGW)
	find_file(LIB_GCC_S_SEH_1_DLL NAMES "libgcc_s_seh-1.dll")
	find_file(LIB_STDCPP_6_DLL NAMES "libstdc++-6.dll")
	find_file(LIB_WINPTHREAD_1_DLL NAMES "libwinpthread-1.dll")

	if(LIB_GCC_S_SEH_1_DLL)
		install(FILES ${LIB_GCC_S_SEH_1_DLL} DESTINATION bin)
	endif()
	if(LIB_STDCPP_6_DLL)
		install(FILES ${LIB_STDCPP_6_DLL} DESTINATION bin)
	endif()
	if(LIB_WINPTHREAD_1_DLL)
		install(FILES ${LIB_WINPTHREAD_1_DLL} DESTINATION bin)
	endif()
endif()

function(copy_mingw_dlls TARGETNAME)
	if (LIB_GCC_S_SEH_1_DLL)
		add_custom_command(TARGET ${TARGETNAME} POST_BUILD
			COMMAND ${CMAKE_COMMAND} -E copy_if_different "${LIB_GCC_S_SEH_1_DLL}" "$<TARGET_FILE_DIR:${TARGETNAME}>"
		)
	endif()
	if (LIB_STDCPP_6_DLL)
		add_custom_command(TARGET ${TARGETNAME} POST_BUILD
			COMMAND ${CMAKE_COMMAND} -E copy_if_different "${LIB_STDCPP_6_DLL}" "$<TARGET_FILE_DIR:${TARGETNAME}>"
		)
	endif()
	if (LIB_WINPTHREAD_1_DLL)
		add_custom_command(TARGET ${TARGETNAME} POST_BUILD
			COMMAND ${CMAKE_COMMAND} -E copy_if_different "${LIB_WINPTHREAD_1_DLL}" "$<TARGET_FILE_DIR:${TARGETNAME}>"
		)
	endif()
endfunction()

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

if (ERICWTOOLS_TIMETRACE)
	message(STATUS "Enabling -ftime-trace")
	add_compile_options(-ftime-trace)
endif()

if (CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND CMAKE_CXX_SIMULATE_ID STREQUAL "MSVC")
	add_compile_options(/EHsc)
endif()

# 10.9: minimum version that supports unordered_map
# 10.14: required by tbb 2021.3.0 (due to use of alignas)
# 10.15: required by std::filesytstem
set(CMAKE_OSX_DEPLOYMENT_TARGET 10.15)

find_package(TBB REQUIRED)

set(TEST_QUAKE_MAP_EXPORT_DIR "" CACHE PATH "When running unit tests, export Quake maps to this directory (useful for testing in game)")
set(TEST_QUAKE2_MAP_EXPORT_DIR "" CACHE PATH "When running unit tests, export Quake 2 maps to this directory (useful for testing in game)")
set(TEST_HEXEN2_MAP_EXPORT_DIR "" CACHE PATH "When running unit tests, export Hexen 2 maps to this directory (useful for testing in game)")
set(TEST_HALFLIFE_MAP_EXPORT_DIR "" CACHE PATH "When running unit tests, export Half-Life maps to this directory (useful for testing in game)")

add_subdirectory(3rdparty)
add_subdirectory(common)
add_subdirectory(bspinfo)
add_subdirectory(bsputil)
add_subdirectory(light)
add_subdirectory(qbsp)
add_subdirectory(vis)
add_subdirectory(maputil)

option(DISABLE_TESTS "Disables Tests" OFF)
option(DISABLE_DOCS "Disables Docs" OFF)
option(ENABLE_LIGHTPREVIEW "Enable light preview tool" ON)

if (ENABLE_LIGHTPREVIEW)
	add_subdirectory(lightpreview)
endif ()

if(NOT DISABLE_TESTS)
	# just creates testmaps.hh with absolute path to the testmaps source directory.
	# include it as #include <testmaps.hh>
	configure_file(testmaps.hh.in testmaps.hh @ONLY)
	include_directories(${CMAKE_CURRENT_BINARY_DIR})
	add_subdirectory(tests)

	#CTest
	enable_testing()
endif ()

if(NOT DISABLE_DOCS)
	add_subdirectory(docs)
endif ()

install(FILES README.md DESTINATION bin)

#CPack configuration

set(CPACK_GENERATOR ZIP)
set(CPACK_PACKAGE_NAME ericw-tools)
set(CPACK_PACKAGE_VERSION ${GIT_DESCRIBE})
include(CPack)
