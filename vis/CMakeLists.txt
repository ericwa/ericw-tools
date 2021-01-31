cmake_minimum_required (VERSION 2.8)
project (vis CXX)

set(VIS_INCLUDES
	${CMAKE_SOURCE_DIR}/include/vis/leafbits.hh
	${CMAKE_SOURCE_DIR}/include/vis/vis.hh)

set(VIS_SOURCES
	flow.cc
	vis.cc
	soundpvs.cc
	state.cc
	${CMAKE_SOURCE_DIR}/common/entdata.cc
	${CMAKE_SOURCE_DIR}/common/cmdlib.cc
	${CMAKE_SOURCE_DIR}/common/mathlib.cc
	${CMAKE_SOURCE_DIR}/common/qvec.cc
	${CMAKE_SOURCE_DIR}/common/polylib.cc
	${CMAKE_SOURCE_DIR}/common/bsputils.cc
	${CMAKE_SOURCE_DIR}/common/bspfile.cc
	${CMAKE_SOURCE_DIR}/common/log.cc
	${CMAKE_SOURCE_DIR}/common/threads.cc
	${COMMON_INCLUDES}
	${VIS_INCLUDES})

add_executable(vis ${VIS_SOURCES})
target_link_libraries (vis ${CMAKE_THREAD_LIBS_INIT})
find_library(M_LIB m)
if (M_LIB)
    target_link_libraries (vis ${M_LIB})
endif (M_LIB)
install(TARGETS vis RUNTIME DESTINATION bin)
