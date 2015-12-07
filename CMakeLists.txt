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

set(STACKSIZE 8388608)
add_definitions(-DQ_STACKSIZE=${STACKSIZE})

#request 8MB stack
if (WIN32)
	set(CMAKE_EXE_LINKER_FLAGS -Wl,--stack,${STACKSIZE})
endif (WIN32)

if (MSVC)
	set(CMAKE_EXE_LINKER_FLAGS /STACK:${STACKSIZE} )
endif (MSVC)

add_subdirectory(bspinfo)
add_subdirectory(bsputil)
add_subdirectory(light)
add_subdirectory(qbsp)
add_subdirectory(vis)
