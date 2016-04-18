cmake_minimum_required (VERSION 2.6)
project (vis)

set(VIS_INCLUDES
	${CMAKE_SOURCE_DIR}/include/vis/leafbits.h
	${CMAKE_SOURCE_DIR}/include/vis/vis.h)

set(VIS_SOURCES
	flow.c
	vis.c
	soundpvs.c
	state.c
	${CMAKE_SOURCE_DIR}/common/cmdlib.c
	${CMAKE_SOURCE_DIR}/common/mathlib.c
	${CMAKE_SOURCE_DIR}/common/bspfile.c
	${CMAKE_SOURCE_DIR}/common/log.c
	${CMAKE_SOURCE_DIR}/common/threads.c
	${COMMON_INCLUDES}
	${VIS_INCLUDES})

add_executable(vis ${VIS_SOURCES})
target_link_libraries (vis ${CMAKE_THREAD_LIBS_INIT})
target_link_libraries (vis m)
install(TARGETS vis RUNTIME DESTINATION bin)
