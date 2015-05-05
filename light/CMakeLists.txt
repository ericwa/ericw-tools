cmake_minimum_required (VERSION 2.6)
project (light)

set(LIGHT_INCLUDES
	${CMAKE_SOURCE_DIR}/include/light/entities.h
	${CMAKE_SOURCE_DIR}/include/light/light.h
	${CMAKE_SOURCE_DIR}/include/light/litfile.h)

set(LIGHT_SOURCES
	entities.c
	litfile.c
	ltface.c
	trace.c
	light.c
	${CMAKE_SOURCE_DIR}/common/bspfile.c
	${CMAKE_SOURCE_DIR}/common/cmdlib.c
	${CMAKE_SOURCE_DIR}/common/mathlib.c
	${CMAKE_SOURCE_DIR}/common/log.c
	${CMAKE_SOURCE_DIR}/common/threads.c
	${COMMON_INCLUDES}
	${LIGHT_INCLUDES})

add_executable(light ${LIGHT_SOURCES})
target_link_libraries (light ${CMAKE_THREAD_LIBS_INIT})
