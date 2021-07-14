cmake_minimum_required (VERSION 3.0)
cmake_policy(SET CMP0028 NEW)

project (ericw-tools)

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
	"${CMAKE_SOURCE_DIR}/3rdparty/glm")

set(COMMON_INCLUDES
	${CMAKE_SOURCE_DIR}/include/common/aabb.hh
	${CMAKE_SOURCE_DIR}/include/common/entdata.h
	${CMAKE_SOURCE_DIR}/include/common/mesh.hh
	${CMAKE_SOURCE_DIR}/include/common/octree.hh
	${CMAKE_SOURCE_DIR}/include/common/qvec.hh
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

set(QBSP_INCLUDES
	${CMAKE_SOURCE_DIR}/include/qbsp/bspfile.hh
	${CMAKE_SOURCE_DIR}/include/qbsp/file.hh
	${CMAKE_SOURCE_DIR}/include/qbsp/parser.hh
	${CMAKE_SOURCE_DIR}/include/qbsp/qbsp.hh
	${CMAKE_SOURCE_DIR}/include/qbsp/wad.hh
	${CMAKE_SOURCE_DIR}/include/qbsp/warnerr.hh
	${CMAKE_SOURCE_DIR}/include/qbsp/brush.hh
	${CMAKE_SOURCE_DIR}/include/qbsp/csg4.hh
	${CMAKE_SOURCE_DIR}/include/qbsp/map.hh
	${CMAKE_SOURCE_DIR}/include/qbsp/winding.hh
	${CMAKE_SOURCE_DIR}/include/qbsp/merge.hh
	${CMAKE_SOURCE_DIR}/include/qbsp/outside.hh
	${CMAKE_SOURCE_DIR}/include/qbsp/portals.hh
	${CMAKE_SOURCE_DIR}/include/qbsp/region.hh
	${CMAKE_SOURCE_DIR}/include/qbsp/solidbsp.hh
	${CMAKE_SOURCE_DIR}/include/qbsp/surfaces.hh
	${CMAKE_SOURCE_DIR}/include/qbsp/tjunc.hh
	${CMAKE_SOURCE_DIR}/include/qbsp/util.hh
	${CMAKE_SOURCE_DIR}/include/qbsp/writebsp.hh)

set(QBSP_SOURCES
	${CMAKE_SOURCE_DIR}/common/threads.cc
	${CMAKE_SOURCE_DIR}/common/cmdlib.cc
	${CMAKE_SOURCE_DIR}/common/log.cc
	${CMAKE_SOURCE_DIR}/common/qvec.cc
	${CMAKE_SOURCE_DIR}/common/mathlib.cc
	${CMAKE_SOURCE_DIR}/common/polylib.cc
	${CMAKE_SOURCE_DIR}/qbsp/brush.cc
	${CMAKE_SOURCE_DIR}/qbsp/bspfile.cc
	${CMAKE_SOURCE_DIR}/qbsp/csg4.cc
	${CMAKE_SOURCE_DIR}/qbsp/file.cc
	${CMAKE_SOURCE_DIR}/qbsp/globals.cc
	${CMAKE_SOURCE_DIR}/qbsp/map.cc
	${CMAKE_SOURCE_DIR}/qbsp/merge.cc
	${CMAKE_SOURCE_DIR}/qbsp/outside.cc
	${CMAKE_SOURCE_DIR}/qbsp/parser.cc
	${CMAKE_SOURCE_DIR}/qbsp/portals.cc
	${CMAKE_SOURCE_DIR}/qbsp/qbsp.cc
	${CMAKE_SOURCE_DIR}/qbsp/solidbsp.cc
	${CMAKE_SOURCE_DIR}/qbsp/surfaces.cc
	${CMAKE_SOURCE_DIR}/qbsp/tjunc.cc
	${CMAKE_SOURCE_DIR}/qbsp/util.cc
	${CMAKE_SOURCE_DIR}/qbsp/wad.cc
	${CMAKE_SOURCE_DIR}/qbsp/winding.cc
	${CMAKE_SOURCE_DIR}/qbsp/writebsp.cc
	${CMAKE_SOURCE_DIR}/qbsp/exportobj.cc
	${COMMON_INCLUDES}
	${QBSP_INCLUDES})

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
	set(CMAKE_CXX_STANDARD 17)
	set(CMAKE_CXX_STANDARD_REQUIRED ON)
	set(CMAKE_C_STANDARD 99)
endif ()

add_definitions(-DERICWTOOLS_VERSION=${GIT_DESCRIBE})

if (MSVC)
	add_definitions("/DWIN32")
	# TODO: remove these 
	add_definitions("/D_CRT_SECURE_NO_WARNINGS")
	add_definitions("/wd4244") # disable "conversion from .. to .., possible loss of data" warning
	add_definitions("/wd4018") # disable "signed/unsigned mismatch" warning
	add_definitions("/wd4200") # disable "nonstandard extension used: zero-sized array in struct/union" warning
endif (MSVC)

# 10.9: minimum version that supports unordered_map
# 10.14: required by tbb 2021.3.0 (due to use of alignas)
set(CMAKE_OSX_DEPLOYMENT_TARGET 10.14)

find_package(TBB REQUIRED)

add_subdirectory(3rdparty)
add_subdirectory(bspinfo)
add_subdirectory(bsputil)
add_subdirectory(light)

if (ENABLE_LIGHTPREVIEW)
	add_subdirectory(lightpreview)
endif ()

add_subdirectory(qbsp)
add_subdirectory(vis)
add_subdirectory(man)

install(FILES README.md DESTINATION bin)
install(FILES changelog.md DESTINATION bin)

#CPack configuration

set(CPACK_GENERATOR ZIP)
set(CPACK_PACKAGE_NAME ericw-tools)
set(CPACK_PACKAGE_VERSION ${GIT_DESCRIBE})
include(CPack)
