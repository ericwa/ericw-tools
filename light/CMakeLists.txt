set(LIGHT_INCLUDES
	${CMAKE_SOURCE_DIR}/include/light/imglib.hh
	${CMAKE_SOURCE_DIR}/include/light/entities.hh
	${CMAKE_SOURCE_DIR}/include/light/light.hh
	${CMAKE_SOURCE_DIR}/include/light/phong.hh
	${CMAKE_SOURCE_DIR}/include/light/bounce.hh
	${CMAKE_SOURCE_DIR}/include/light/surflight.hh
	${CMAKE_SOURCE_DIR}/include/light/ltface.hh
	${CMAKE_SOURCE_DIR}/include/light/trace.hh
	${CMAKE_SOURCE_DIR}/include/light/litfile.hh
	${CMAKE_SOURCE_DIR}/include/light/settings.hh)

set(LIGHT_SOURCES
	entities.cc
	litfile.cc
	ltface.cc
	trace.cc
	light.cc
	phong.cc
	bounce.cc
	surflight.cc
	settings.cc
	imglib.cc
	${CMAKE_SOURCE_DIR}/common/bspfile.cc
	${CMAKE_SOURCE_DIR}/common/entdata.cc
	${CMAKE_SOURCE_DIR}/common/cmdlib.cc
	${CMAKE_SOURCE_DIR}/common/mathlib.cc
	${CMAKE_SOURCE_DIR}/common/qvec.cc
	${CMAKE_SOURCE_DIR}/common/mesh.cc
	${CMAKE_SOURCE_DIR}/common/log.cc
	${CMAKE_SOURCE_DIR}/common/threads.cc
	${CMAKE_SOURCE_DIR}/common/polylib.cc
	${CMAKE_SOURCE_DIR}/common/bsputils.cc
	${COMMON_INCLUDES}
	${LIGHT_INCLUDES})


FIND_PACKAGE(embree 3.0 REQUIRED)

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

	# This needs to be before the add_executable
	if (${CMAKE_SYSTEM_NAME} MATCHES "Linux")
		SET(CMAKE_INSTALL_RPATH "$ORIGIN")
	endif ()
endif(embree_FOUND)

add_executable(light ${LIGHT_SOURCES} main.cc)
target_link_libraries (light ${CMAKE_THREAD_LIBS_INIT})

if (embree_FOUND)
	target_link_libraries (light ${EMBREE_LIBRARY})
	add_definitions(-DHAVE_EMBREE)
	
	set(EMBREE_LICENSE "${embree_DIR}/doc/LICENSE.txt")

	if(WIN32)
		file(GLOB EMBREE_DLLS "${embree_DIR}/bin/*.dll")
		foreach(EMBREE_DLL ${EMBREE_DLLS})
			add_custom_command(TARGET light POST_BUILD COMMAND ${CMAKE_COMMAND} ARGS -E copy_if_different ${EMBREE_DLL} $<TARGET_FILE_DIR:light>)
		endforeach(EMBREE_DLL)
		
		add_custom_command(TARGET light POST_BUILD COMMAND ${CMAKE_COMMAND} ARGS -E copy_if_different ${EMBREE_LICENSE} $<TARGET_FILE_DIR:light>/LICENSE-embree.txt)

		install(FILES ${EMBREE_DLLS} DESTINATION bin)
	endif()
	if(UNIX)
		if (APPLE)
			set(SHARED_LIB_EXT dylib)
		elseif (${CMAKE_SYSTEM_NAME} MATCHES "Linux")
			set(SHARED_LIB_EXT "so.*")
		else ()
			message(FATAL_ERROR "only Linux and macOS currently supported")	
		endif()


		file(GLOB EMBREE_DYLIBS_WITH_SYMLINKS "${embree_DIR}/lib/*.${SHARED_LIB_EXT}")

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
		if(APPLE)
			add_custom_command(TARGET light POST_BUILD COMMAND bash ARGS -c \"install_name_tool -add_rpath @loader_path $<TARGET_FILE:light> || true\")
		endif()

		install(FILES ${EMBREE_DYLIBS} DESTINATION bin)
	endif()

	install(FILES ${EMBREE_LICENSE} DESTINATION bin RENAME LICENSE-embree.txt)
endif(embree_FOUND)

install(TARGETS light RUNTIME DESTINATION bin)
install(FILES ${CMAKE_SOURCE_DIR}/gpl_v3.txt DESTINATION bin)

# test

#see https://cmake.org/Wiki/CMakeEmulateMakeCheck
add_custom_target(check COMMAND ${CMAKE_CTEST_COMMAND})

set(LIGHT_TEST_SOURCE 
	${LIGHT_SOURCES}
	test.cc
	test_entities.cc
	test_ltface.cc
	test_light.cc
	test_common.cc)

add_executable(testlight EXCLUDE_FROM_ALL ${LIGHT_TEST_SOURCE})
add_test(testlight testlight)
add_dependencies(check testlight)

target_link_libraries (testlight ${CMAKE_THREAD_LIBS_INIT} gtest)
if (embree_FOUND)
	target_link_libraries (testlight ${EMBREE_LIBRARY})
	add_definitions(-DHAVE_EMBREE)
endif (embree_FOUND)
