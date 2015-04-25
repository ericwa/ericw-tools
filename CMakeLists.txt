cmake_minimum_required (VERSION 2.6)
project (tyrutils)

include_directories(
	"${PROJECT_SOURCE_DIR}/include")

set(COMMON_INCLUDES
	include/common/bspfile.h
	include/common/cmdlib.h
	include/common/lbmlib.h 
	include/common/log.h
	include/common/mathlib.h
	include/common/polylib.h 
	include/common/scriplib.h
	include/common/threads.h 
	include/common/trilib.h 
	include/common/wadlib.h)

set(LIGHT_INCLUDES
	include/light/entities.h
	include/light/light.h
	include/light/litfile.h)

set(VIS_INCLUDES
	include/vis/leafbits.h
	include/vis/vis.h)

set(QBSP_INCLUDES
	qbsp/bspfile.h
	qbsp/file.h
	qbsp/parser.h 
	qbsp/qbsp.h
	qbsp/wad.h
	qbsp/warnerr.h)

set(LIGHT_SOURCES
	light/entities.c
	light/litfile.c
	light/ltface.c
	light/trace.c
	light/light.c
	common/bspfile.c
	common/cmdlib.c
	common/mathlib.c
	common/log.c
	common/threads.c
	${COMMON_INCLUDES}
	${LIGHT_INCLUDES})

set(VIS_SOURCES
	vis/flow.c
	vis/vis.c
	vis/soundpvs.c
	vis/state.c
	common/cmdlib.c
	common/mathlib.c
	common/bspfile.c
	common/log.c
	common/threads.c
	${COMMON_INCLUDES}
	${VIS_INCLUDES})

set(BSPINFO_SOURCES
	bspinfo/bspinfo.c
	common/cmdlib.c
	common/bspfile.c
	common/log.c
	common/threads.c
	${COMMON_INCLUDES})

set(BSPUTIL_SOURCES
	bsputil/bsputil.c
	common/cmdlib.c
	common/bspfile.c
	common/log.c
	common/threads.c
	${COMMON_INCLUDES})

set(QBSP_SOURCES
	common/threads.c
	common/log.c
	qbsp/brush.c
	qbsp/bspfile.c
	qbsp/cmdlib.c
	qbsp/csg4.c
	qbsp/file.c
	qbsp/globals.c
	qbsp/map.c
	qbsp/mathlib.c
	qbsp/merge.c
	qbsp/outside.c
	qbsp/parser.c
	qbsp/portals.c
	qbsp/qbsp.c
	qbsp/solidbsp.c
	qbsp/surfaces.c
	qbsp/tjunc.c
	qbsp/util.c
	qbsp/wad.c
	qbsp/winding.c
	qbsp/writebsp.c
	${QBSP_INCLUDES})

find_package (Threads)

add_executable(light ${LIGHT_SOURCES})
add_executable(vis ${VIS_SOURCES})
add_executable(bspinfo ${BSPINFO_SOURCES})
add_executable(bsputil ${BSPUTIL_SOURCES})
add_executable(qbsp ${QBSP_SOURCES})

target_link_libraries (light ${CMAKE_THREAD_LIBS_INIT})

if (CMAKE_USE_PTHREADS_INIT)
	add_definitions(-DUSE_PTHREADS)
endif (CMAKE_USE_PTHREADS_INIT)

#FIXME: Should be only for qbsp
add_definitions(-DDOUBLEVEC_T)

#request 8MB stack
if (WIN32)
	set(CMAKE_EXE_LINKER_FLAGS -Wl,--stack,8388608)
endif (WIN32)
