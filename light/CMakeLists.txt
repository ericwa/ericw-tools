cmake_minimum_required (VERSION 3.1)
project (light CXX C)

set(LIGHT_INCLUDES
	${CMAKE_SOURCE_DIR}/include/light/entities.hh
	${CMAKE_SOURCE_DIR}/include/light/light.hh
	${CMAKE_SOURCE_DIR}/include/light/ltface.hh
	${CMAKE_SOURCE_DIR}/include/light/trace.hh
	${CMAKE_SOURCE_DIR}/include/light/litfile.hh)

set(LIGHT_SOURCES
	entities.cc
	litfile.cc
	ltface.cc
	trace.cc
	light.cc
	${CMAKE_SOURCE_DIR}/common/bspfile.c
	${CMAKE_SOURCE_DIR}/common/cmdlib.c
	${CMAKE_SOURCE_DIR}/common/mathlib.c
	${CMAKE_SOURCE_DIR}/common/log.c
	${CMAKE_SOURCE_DIR}/common/threads.c
	${CMAKE_SOURCE_DIR}/common/polylib.c
	${CMAKE_SOURCE_DIR}/common/bsputils.cc
	${COMMON_INCLUDES}
	${LIGHT_INCLUDES})


FIND_PACKAGE(embree 2.0)

if (embree_FOUND)
	MESSAGE(STATUS "Embree library found: ${EMBREE_LIBRARY}")
	INCLUDE_DIRECTORIES(${EMBREE_INCLUDE_DIRS})
	set(LIGHT_INCLUDES
		${CMAKE_SOURCE_DIR}/include/light/trace_embree.hh
		${LIGHT_INCLUDES})
	set(LIGHT_SOURCES
		trace_embree.cc
		${CMAKE_SOURCE_DIR}/include/light/trace_embree.hh
		${LIGHT_SOURCES})
endif(embree_FOUND)


add_executable(light ${LIGHT_SOURCES})
target_link_libraries (light ${CMAKE_THREAD_LIBS_INIT})

if (embree_FOUND)
	target_link_libraries (light ${EMBREE_LIBRARY})
	add_definitions(-DHAVE_EMBREE)
	
	set(EMBREE_LICENSE "${embree_DIR}/../../../doc/LICENSE.txt")

	if(WIN32)
		file(GLOB EMBREE_DLLS "${embree_DIR}/../../*.dll")
		foreach(EMBREE_DLL ${EMBREE_DLLS})
			add_custom_command(TARGET light POST_BUILD COMMAND ${CMAKE_COMMAND} ARGS -E copy_if_different ${EMBREE_DLL} $<TARGET_FILE_DIR:light>)
		endforeach(EMBREE_DLL)
		
		add_custom_command(TARGET light POST_BUILD COMMAND ${CMAKE_COMMAND} ARGS -E copy_if_different ${EMBREE_LICENSE} $<TARGET_FILE_DIR:light>/LICENSE-embree.txt)

		install(FILES ${EMBREE_DLLS} DESTINATION bin)
	endif()
	if(APPLE)
		file(GLOB EMBREE_DYLIBS_WITH_SYMLINKS "${embree_DIR}/../../*.dylib")

		# Gather all .dylib's that are not symlinks
		foreach(EMBREE_DYLIB ${EMBREE_DYLIBS_WITH_SYMLINKS})
			if(NOT IS_SYMLINK ${EMBREE_DYLIB})
				list(APPEND EMBREE_DYLIBS ${EMBREE_DYLIB})
			endif()
		endforeach()

		foreach(EMBREE_DYLIB ${EMBREE_DYLIBS})
			message(STATUS "Copying dylib: ${EMBREE_DYLIB}")
			add_custom_command(TARGET light POST_BUILD COMMAND ${CMAKE_COMMAND} ARGS -E copy_if_different ${EMBREE_DYLIB} $<TARGET_FILE_DIR:light>)
		endforeach()

		# so the executable will search for dylib's in the same directory as the executable 
		add_custom_command(TARGET light POST_BUILD COMMAND bash ARGS -c \"install_name_tool -add_rpath @loader_path $<TARGET_FILE:light> || true\")

		install(FILES ${EMBREE_DYLIBS} DESTINATION bin)
	endif()

	install(FILES ${EMBREE_LICENSE} DESTINATION bin RENAME LICENSE-embree.txt)
endif(embree_FOUND)

install(TARGETS light RUNTIME DESTINATION bin)
install(FILES ${CMAKE_SOURCE_DIR}/gpl_v3.txt DESTINATION bin)
