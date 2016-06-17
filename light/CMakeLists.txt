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


FIND_PACKAGE(embree 2.0)

if (embree_FOUND)
	MESSAGE(STATUS "Embree found")
	INCLUDE_DIRECTORIES(${EMBREE_INCLUDE_DIRS})
	set(LIGHT_SOURCES
		trace_embree.cc
		${LIGHT_SOURCES})
endif(embree_FOUND)


add_executable(light ${LIGHT_SOURCES})
target_link_libraries (light ${CMAKE_THREAD_LIBS_INIT})
set_property(TARGET light PROPERTY CXX_STANDARD 11)
set_property(TARGET light PROPERTY CXX_STANDARD_REQUIRED ON)
set_property(TARGET light PROPERTY C_STANDARD 99)

if (embree_FOUND)
	target_link_libraries (light ${EMBREE_LIBRARY})
	add_definitions(-DHAVE_EMBREE)
	if(WIN32)
		file(GLOB EMBREE_DLLS "${embree_DIR}/../../*.dll")
		foreach(EMBREE_DLL ${EMBREE_DLLS})
			add_custom_command(TARGET light POST_BUILD COMMAND ${CMAKE_COMMAND} ARGS -E copy_if_different ${EMBREE_DLL} $<TARGET_FILE_DIR:light>)
		endforeach(EMBREE_DLL)
		
		set(EMBREE_LICENSE "${embree_DIR}/../../../doc/LICENSE.txt")
		add_custom_command(TARGET light POST_BUILD COMMAND ${CMAKE_COMMAND} ARGS -E copy_if_different ${EMBREE_LICENSE} $<TARGET_FILE_DIR:light>/LICENSE-embree.txt)
	endif()
endif(embree_FOUND)

install(TARGETS light RUNTIME DESTINATION bin)

if (embree_FOUND)
	if(WIN32)
		install(FILES ${EMBREE_DLLS} DESTINATION bin)
		install(FILES ${EMBREE_LICENSE} DESTINATION bin)
	endif()
endif(embree_FOUND)
