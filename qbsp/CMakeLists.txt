cmake_minimum_required (VERSION 2.8)
project (qbsp CXX)

set(QBSP_INCLUDES
	bspfile.hh
	file.hh
	parser.hh
	qbsp.hh
	wad.hh
	warnerr.hh)

set(QBSP_SOURCES
	${CMAKE_SOURCE_DIR}/common/threads.cc
	${CMAKE_SOURCE_DIR}/common/log.cc
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
