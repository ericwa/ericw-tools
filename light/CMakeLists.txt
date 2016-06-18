cmake_minimum_required (VERSION 2.6)
project (light CXX C)

set(LIGHT_INCLUDES
	${CMAKE_SOURCE_DIR}/include/light/entities.h
	${CMAKE_SOURCE_DIR}/include/light/light.h
	${CMAKE_SOURCE_DIR}/include/light/litfile.h)

set(LIGHT_SOURCES
	entities.c
	litfile.c
	ltface.c
	trace.c
	light.cc
	${CMAKE_SOURCE_DIR}/common/bspfile.c
	${CMAKE_SOURCE_DIR}/common/cmdlib.c
	${CMAKE_SOURCE_DIR}/common/mathlib.c
	${CMAKE_SOURCE_DIR}/common/log.c
	${CMAKE_SOURCE_DIR}/common/threads.c
	${CMAKE_SOURCE_DIR}/common/polylib.c
	${COMMON_INCLUDES}
	${LIGHT_INCLUDES})

if (TYRUTILS_USE_EMBREE)
	INCLUDE_DIRECTORIES(${CMAKE_SOURCE_DIR}/thirdparty/embree/include)
	set(LIGHT_SOURCES
			trace_embree.cc
			${LIGHT_SOURCES})
endif()

add_executable(light ${LIGHT_SOURCES})
target_link_libraries (light ${CMAKE_THREAD_LIBS_INIT})
set_property(TARGET light PROPERTY CXX_STANDARD 11)
set_property(TARGET light PROPERTY CXX_STANDARD_REQUIRED ON)
set_property(TARGET light PROPERTY C_STANDARD 99)

if (TYRUTILS_USE_EMBREE)
	add_dependencies (light embree)
	target_link_libraries (light PRIVATE embree)
	add_definitions(-DEMBREE_STATIC_LIB)
	add_definitions(-DHAVE_EMBREE)
endif()

install(TARGETS light RUNTIME DESTINATION bin)
install(FILES ${CMAKE_SOURCE_DIR}/thirdparty/embree/LICENSE.txt DESTINATION bin RENAME LICENSE-embree.txt)
install(FILES ${CMAKE_SOURCE_DIR}/gpl_v3.txt DESTINATION bin)

