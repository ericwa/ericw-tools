cmake_minimum_required (VERSION 2.6)
project (qbsp CXX)

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
	brush.c
	bspfile.c
	cmdlib.c
	csg4.c
	file.c
	globals.c
	map.c
	mathlib.c
	merge.c
	outside.c
	parser.c
	portals.c
	qbsp.cc
	solidbsp.c
	surfaces.c
	tjunc.c
	util.c
	wad.c
	winding.c
	writebsp.c
	exportobj.c
	${QBSP_INCLUDES})

add_definitions(-DDOUBLEVEC_T)

add_executable(qbsp ${QBSP_SOURCES})
set_property(TARGET qbsp PROPERTY CXX_STANDARD 11)
set_property(TARGET qbsp PROPERTY CXX_STANDARD_REQUIRED ON)

install(TARGETS qbsp RUNTIME DESTINATION bin)
