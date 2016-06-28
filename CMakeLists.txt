cmake_minimum_required (VERSION 2.6)
project (tyrutils)

# Grab the git describe output and store it in GIT_DESCRIBE
# Thanks to http://xit0.org/2013/04/cmake-use-git-branch-and-commit-details-in-project/
execute_process(
  COMMAND git describe --dirty
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  OUTPUT_VARIABLE GIT_DESCRIBE
  OUTPUT_STRIP_TRAILING_WHITESPACE
)

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

if (UNIX)
	add_definitions(-DLINUX)
endif (UNIX)

set(STACKSIZE 8388608)
add_definitions(-DQ_STACKSIZE=${STACKSIZE})
add_definitions(-DTYRUTILS_VERSION=${GIT_DESCRIBE})

#request 8MB stack
if (WIN32)
	set(CMAKE_EXE_LINKER_FLAGS -Wl,--stack,${STACKSIZE})
endif (WIN32)

#MSVC: /MT is to static link the CRT.
if (MSVC)
	set(CMAKE_EXE_LINKER_FLAGS "/STACK:${STACKSIZE}")
	set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} /MT")
	set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} /MTd")
	set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /MT")
	set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /MTd")
endif (MSVC)

#minimum version that supports unordered_map
set(CMAKE_OSX_DEPLOYMENT_TARGET 10.9)

add_subdirectory(bspinfo)
add_subdirectory(bsputil)
add_subdirectory(light)
add_subdirectory(qbsp)
add_subdirectory(vis)
add_subdirectory(man)

install(FILES README.md DESTINATION bin)
install(FILES changelog.txt DESTINATION bin)

#CPack configuration

set(CPACK_GENERATOR ZIP)
set(CPACK_PACKAGE_NAME tyrutils)
set(CPACK_PACKAGE_VERSION ${GIT_DESCRIBE})
include(CPack)
