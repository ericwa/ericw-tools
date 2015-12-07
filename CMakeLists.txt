cmake_minimum_required (VERSION 2.6)
project (tyrutils)

include_directories(
	"${CMAKE_SOURCE_DIR}/include")

set(COMMON_INCLUDES
	${CMAKE_SOURCE_DIR}/include/common/bspfile.h
	${CMAKE_SOURCE_DIR}/include/common/cmdlib.h
	${CMAKE_SOURCE_DIR}/include/common/lbmlib.h 
	${CMAKE_SOURCE_DIR}/include/common/log.h
	${CMAKE_SOURCE_DIR}/include/common/mathlib.h
	${CMAKE_SOURCE_DIR}/include/common/polylib.h 
	${CMAKE_SOURCE_DIR}/include/common/scriplib.h
	${CMAKE_SOURCE_DIR}/include/common/threads.h 
	${CMAKE_SOURCE_DIR}/include/common/trilib.h 
	${CMAKE_SOURCE_DIR}/include/common/wadlib.h)

find_package (Threads)

if (CMAKE_USE_PTHREADS_INIT)
	add_definitions(-DUSE_PTHREADS)
endif (CMAKE_USE_PTHREADS_INIT)

# (see http://sourceforge.net/p/mingw-w64/wiki2/printf%20and%20scanf%20family/)
if (MINGW)
	add_definitions(-D__USE_MINGW_ANSI_STDIO=1)
endif (MINGW)

set(STACKSIZE 8388608)
add_definitions(-DQ_STACKSIZE=${STACKSIZE})

#request 8MB stack
if (WIN32)
	set(CMAKE_EXE_LINKER_FLAGS -Wl,--stack,${STACKSIZE})
endif (WIN32)

#MSVC: /MT is to static link the CRT.
if (MSVC)
	set(CMAKE_EXE_LINKER_FLAGS "/STACK:${STACKSIZE}")
	set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} /MT")
	set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} /MTd")
endif (MSVC)

add_subdirectory(bspinfo)
add_subdirectory(bsputil)
add_subdirectory(light)
add_subdirectory(qbsp)
add_subdirectory(vis)
