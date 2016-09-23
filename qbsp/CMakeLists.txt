cmake_minimum_required (VERSION 2.8)
project (qbsp CXX C)

set(QBSP_INCLUDES
	bspfile.h
	file.h
	parser.h
	qbsp.h
	wad.h
	warnerr.h)

set(QBSP_SOURCES
	${CMAKE_SOURCE_DIR}/common/threads.c
	${CMAKE_SOURCE_DIR}/common/log.c
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
set_property(TARGET qbsp PROPERTY CXX_STANDARD 11)
set_property(TARGET qbsp PROPERTY CXX_STANDARD_REQUIRED ON)
set_property(TARGET qbsp PROPERTY C_STANDARD 99)
target_link_libraries(qbsp ${CMAKE_THREAD_LIBS_INIT})
install(TARGETS qbsp RUNTIME DESTINATION bin)
