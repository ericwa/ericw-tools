set(LIGHT_INCLUDES
	${CMAKE_SOURCE_DIR}/include/light/entities.hh
	${CMAKE_SOURCE_DIR}/include/light/light.hh
	${CMAKE_SOURCE_DIR}/include/light/phong.hh
	${CMAKE_SOURCE_DIR}/include/light/bounce.hh
	${CMAKE_SOURCE_DIR}/include/light/surflight.hh
	${CMAKE_SOURCE_DIR}/include/light/ltface.hh
	${CMAKE_SOURCE_DIR}/include/light/trace.hh
	${CMAKE_SOURCE_DIR}/include/light/litfile.hh)

set(LIGHT_SOURCES
	entities.cc
	litfile.cc
	ltface.cc
	trace.cc
	light.cc
	phong.cc
	bounce.cc
	surflight.cc
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

add_library(liblight STATIC ${LIGHT_SOURCES})
target_link_libraries(liblight PRIVATE common ${CMAKE_THREAD_LIBS_INIT} fmt::fmt nlohmann_json::nlohmann_json)

add_executable(light main.cc)
target_link_libraries(light PRIVATE common liblight)

if (embree_FOUND)
	target_link_libraries (liblight PRIVATE embree)
	add_definitions(-DHAVE_EMBREE)

	# macOS/homebrew: hack around find_file applying the .. before resolving the symlink
	# causing it not to find the LICENSE.txt
	get_filename_component(embree_DIR_ABS "${embree_DIR}" REALPATH CACHE)

	find_file(EMBREE_LICENSE LICENSE.txt 
		"${embree_DIR_ABS}/doc"
		"${embree_DIR_ABS}/../../../doc"
		"${embree_DIR_ABS}/../embree3/embree3" # vcpkg puts it here
		"${embree_DIR_ABS}/../../.." # homebrew puts it here
		NO_DEFAULT_PATH)
	if (EMBREE_LICENSE STREQUAL EMBREE_LICENSE-NOTFOUND)
		message(WARNING "Couldn't find embree license. embree_DIR: ${embree_DIR}, embree_DIR_ABS: ${embree_DIR_ABS}")
	else()
		message(STATUS "Found embree license: ${EMBREE_LICENSE}")
	endif()

	add_custom_command(TARGET light POST_BUILD
                       COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:embree>"   "$<TARGET_FILE_DIR:light>"
                       COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:TBB::tbb>" "$<TARGET_FILE_DIR:light>")

	if (NOT EMBREE_LICENSE STREQUAL EMBREE_LICENSE-NOTFOUND)
		add_custom_command(TARGET light POST_BUILD
                       COMMAND ${CMAKE_COMMAND} -E copy_if_different "${EMBREE_LICENSE}"       "$<TARGET_FILE_DIR:light>/LICENSE-embree.txt")
	endif()

	# so the executable will search for dylib's in the same directory as the executable 
	if(APPLE)
		add_custom_command(TARGET light POST_BUILD 
			COMMAND bash ARGS -c \"install_name_tool -add_rpath @loader_path $<TARGET_FILE:light> || true\")
	endif()
	
	install(FILES $<TARGET_FILE:embree> DESTINATION bin)

	# install TBB
	if(UNIX)
		# HACK: preferred method is installing the symlink instead of the actual .so
		get_target_property(TBB_SO_FILE_SYMLINK TBB::tbb IMPORTED_LOCATION_RELEASE)
		get_filename_component(TBB_SO_FILE "${TBB_SO_FILE_SYMLINK}" REALPATH)

		message(STATUS "TBB .so file: ${TBB_SO_FILE}")

		install(FILES ${TBB_SO_FILE} DESTINATION bin)
	else()
		# preferred method
		install(FILES $<TARGET_FILE:TBB::tbb> DESTINATION bin)
	endif()

	if (NOT EMBREE_LICENSE STREQUAL EMBREE_LICENSE-NOTFOUND)
		install(FILES ${EMBREE_LICENSE} DESTINATION bin RENAME LICENSE-embree.txt)
	endif()
endif(embree_FOUND)

install(TARGETS light RUNTIME DESTINATION bin)
install(FILES ${CMAKE_SOURCE_DIR}/gpl_v3.txt DESTINATION bin)

# test

#see https://cmake.org/Wiki/CMakeEmulateMakeCheck
add_custom_target(check COMMAND ${CMAKE_CTEST_COMMAND})

set(LIGHT_TEST_SOURCE 
	test.cc
	test_entities.cc
	test_ltface.cc
	test_light.cc
	test_common.cc)

add_executable(testlight EXCLUDE_FROM_ALL ${LIGHT_TEST_SOURCE})
add_test(testlight testlight)
add_dependencies(check testlight)

target_link_libraries (testlight PRIVATE liblight common ${CMAKE_THREAD_LIBS_INIT} gtest fmt::fmt)
if (embree_FOUND)
	target_link_libraries (testlight PRIVATE embree)
	add_definitions(-DHAVE_EMBREE)
endif (embree_FOUND)
