cmake_minimum_required (VERSION 2.8)
project (qbsp CXX)

set(QBSP_INCLUDES
	${CMAKE_SOURCE_DIR}/include/qbsp/bspfile.hh
	${CMAKE_SOURCE_DIR}/include/qbsp/file.hh
	${CMAKE_SOURCE_DIR}/include/qbsp/parser.hh
	${CMAKE_SOURCE_DIR}/include/qbsp/qbsp.hh
	${CMAKE_SOURCE_DIR}/include/qbsp/wad.hh
	${CMAKE_SOURCE_DIR}/include/qbsp/warnerr.hh
	${CMAKE_SOURCE_DIR}/include/qbsp/brush.hh
	${CMAKE_SOURCE_DIR}/include/qbsp/cmdlib.hh
	${CMAKE_SOURCE_DIR}/include/qbsp/csg4.hh
	${CMAKE_SOURCE_DIR}/include/qbsp/map.hh
	${CMAKE_SOURCE_DIR}/include/qbsp/mathlib.hh
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
	${CMAKE_SOURCE_DIR}/common/log.cc
	${CMAKE_SOURCE_DIR}/common/qvec.cc
	brush.cc
	bspfile.cc
	cmdlib.cc
	csg4.cc
	file.cc
	globals.cc
	map.cc
	mathlib.cc
	merge.cc
	outside.cc
	parser.cc
	portals.cc
	qbsp.cc
	solidbsp.cc
	surfaces.cc
	tjunc.cc
	util.cc
	wad.cc
	winding.cc
	writebsp.cc
	exportobj.cc
	${QBSP_INCLUDES})

add_definitions(-DDOUBLEVEC_T)

add_executable(qbsp ${QBSP_SOURCES})
target_link_libraries(qbsp ${CMAKE_THREAD_LIBS_INIT})
install(TARGETS qbsp RUNTIME DESTINATION bin)
