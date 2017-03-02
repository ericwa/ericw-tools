cmake_minimum_required (VERSION 2.8)
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
	"${CMAKE_SOURCE_DIR}/include"
	"${CMAKE_SOURCE_DIR}/3rdparty/glm"
	"${CMAKE_SOURCE_DIR}/3rdparty/eigen")

set(COMMON_INCLUDES
	${CMAKE_SOURCE_DIR}/include/common/bspfile.hh
	${CMAKE_SOURCE_DIR}/include/common/cmdlib.hh
	${CMAKE_SOURCE_DIR}/include/common/lbmlib.hh
	${CMAKE_SOURCE_DIR}/include/common/log.hh
	${CMAKE_SOURCE_DIR}/include/common/mathlib.hh
	${CMAKE_SOURCE_DIR}/include/common/polylib.hh 
	${CMAKE_SOURCE_DIR}/include/common/scriplib.hh
	${CMAKE_SOURCE_DIR}/include/common/threads.hh 
	${CMAKE_SOURCE_DIR}/include/common/trilib.hh 
	${CMAKE_SOURCE_DIR}/include/common/wadlib.hh
	${CMAKE_SOURCE_DIR}/include/common/bsputils.hh)

find_package (Threads)

if (CMAKE_USE_PTHREADS_INIT)
	add_definitions(-DUSE_PTHREADS)
elseif (CMAKE_USE_WIN32_THREADS_INIT)
	add_definitions(-DUSE_WIN32THREADS)
endif ()

# (see http://sourceforge.net/p/mingw-w64/wiki2/printf%20and%20scanf%20family/)
if (MINGW)
	add_definitions(-D__USE_MINGW_ANSI_STDIO=1)
endif (MINGW)

if (UNIX)
	add_definitions(-DLINUX)
endif (UNIX)

# set our C/C++ dialects
if (CMAKE_VERSION VERSION_LESS "3.1")
	set (CMAKE_CXX_FLAGS "-std=gnu++11 ${CMAKE_CXX_FLAGS}")
	set (CMAKE_C_FLAGS "-std=gnu11 ${CMAKE_C_FLAGS}")
else ()
	set(CMAKE_CXX_STANDARD 11)
	set(CMAKE_CXX_STANDARD_REQUIRED ON)
	set(CMAKE_C_STANDARD 99)
endif ()

add_definitions(-DTYRUTILS_VERSION=${GIT_DESCRIBE})

if (MSVC)
	add_definitions("/DWIN32")
	# TODO: remove these 
	add_definitions("/D_CRT_SECURE_NO_WARNINGS")
	add_definitions("/wd4244") # disable "conversion from .. to .., possible loss of data" warning
	add_definitions("/wd4018") # disable "signed/unsigned mismatch" warning
	add_definitions("/wd4200") # disable "nonstandard extension used: zero-sized array in struct/union" warning
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
