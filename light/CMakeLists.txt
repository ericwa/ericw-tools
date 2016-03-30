cmake_minimum_required (VERSION 2.6)
project (light CXX)

set(LIGHT_INCLUDES
	${CMAKE_SOURCE_DIR}/include/light/entities.h
	${CMAKE_SOURCE_DIR}/include/light/light.h
	${CMAKE_SOURCE_DIR}/include/light/litfile.h)

set(LIGHT_SOURCES
	entities.c
	litfile.c
	ltface.c
	trace_embree.cc
	light.cc
	${CMAKE_SOURCE_DIR}/common/bspfile.c
	${CMAKE_SOURCE_DIR}/common/cmdlib.c
	${CMAKE_SOURCE_DIR}/common/mathlib.c
	${CMAKE_SOURCE_DIR}/common/log.c
	${CMAKE_SOURCE_DIR}/common/threads.c
	${COMMON_INCLUDES}
	${LIGHT_INCLUDES})

FIND_PACKAGE(embree 2.0 REQUIRED)
INCLUDE_DIRECTORIES(${EMBREE_INCLUDE_DIRS})

add_executable(light ${LIGHT_SOURCES})
target_link_libraries (light ${CMAKE_THREAD_LIBS_INIT} ${EMBREE_LIBRARY})
set_property(TARGET light PROPERTY CXX_STANDARD 11)
set_property(TARGET light PROPERTY CXX_STANDARD_REQUIRED ON)

if(WIN32)
    file(GLOB EMBREE_DLLS "${embree_DIR}/../../*.dll")
    foreach(EMBREE_DLL ${EMBREE_DLLS})
        add_custom_command(TARGET light POST_BUILD COMMAND ${CMAKE_COMMAND} ARGS -E copy_if_different ${EMBREE_DLL} $<TARGET_FILE_DIR:light>)
    endforeach(EMBREE_DLL)
endif()

install(TARGETS light RUNTIME DESTINATION bin)
if(WIN32)
    install(FILES ${EMBREE_DLLS} DESTINATION bin)
endif()