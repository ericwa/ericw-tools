option(SKIP_TBB_INSTALL "Skip TBB Library Installation" OFF)
option(SKIP_EMBREE_INSTALL "Skip Embree Library Installation" OFF)

set(LIGHT_INCLUDES
	../include/light/entities.hh
	../include/light/light.hh
	../include/light/lightgrid.hh
	../include/light/phong.hh
	../include/light/bounce.hh
	../include/light/surflight.hh
	../include/light/ltface.hh
	../include/light/trace.hh
	../include/light/litfile.hh)

set(LIGHT_SOURCES
	entities.cc
	litfile.cc
	ltface.cc
	trace.cc
	light.cc
	lightgrid.cc
	phong.cc
	bounce.cc
	surflight.cc
	${LIGHT_INCLUDES})

FIND_PACKAGE(embree 3.0 REQUIRED)

if (embree_FOUND)
	MESSAGE(STATUS "Embree library found: ${EMBREE_LIBRARY}")
	INCLUDE_DIRECTORIES(${EMBREE_INCLUDE_DIRS})
	set(LIGHT_INCLUDES
		../include/light/trace_embree.hh
		${LIGHT_INCLUDES})
	set(LIGHT_SOURCES
		trace_embree.cc
		../include/light/trace_embree.hh
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

	# HACK: Windows embree .dll's from https://github.com/embree/embree/releases ship with a tbb12.dll
	# and we need to copy it from the embree/bin directory to our light.exe/testlight.exe dir in order for them to run
	find_file(EMBREE_TBB_DLL tbb12.dll
              "${EMBREE_ROOT_DIR}/bin"
              NO_DEFAULT_PATH)
	if (NOT EMBREE_TBB_DLL STREQUAL EMBREE_TBB_DLL-NOTFOUND)
		message(STATUS "Found embree EMBREE_TBB_DLL: ${EMBREE_TBB_DLL}")
	endif()

	add_custom_command(TARGET light POST_BUILD
                       COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:embree>"   "$<TARGET_FILE_DIR:light>"
                       COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:TBB::tbb>" "$<TARGET_FILE_DIR:light>"
					   COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:TBB::tbbmalloc>" "$<TARGET_FILE_DIR:light>"
					   )

	if (NOT EMBREE_LICENSE STREQUAL EMBREE_LICENSE-NOTFOUND)
		add_custom_command(TARGET light POST_BUILD
                       COMMAND ${CMAKE_COMMAND} -E copy_if_different "${EMBREE_LICENSE}"       "$<TARGET_FILE_DIR:light>/LICENSE-embree.txt")
	endif()
	if (NOT EMBREE_TBB_DLL STREQUAL EMBREE_TBB_DLL-NOTFOUND)
		add_custom_command(TARGET light POST_BUILD
				           COMMAND ${CMAKE_COMMAND} -E copy_if_different "${EMBREE_TBB_DLL}" "$<TARGET_FILE_DIR:light>")
	endif()

	# so the executable will search for dylib's in the same directory as the executable
	if(APPLE)
		add_custom_command(TARGET light POST_BUILD
			COMMAND bash ARGS -c \"install_name_tool -add_rpath @loader_path $<TARGET_FILE:light> || true\")
	endif()

	if(NOT SKIP_EMBREE_INSTALL)
		install(FILES $<TARGET_FILE:embree> DESTINATION bin)
	endif()

	# install TBB
	if(SKIP_TBB_INSTALL)
		message(STATUS "Skipping TBB Install")
	elseif(UNIX)
		# HACK: manually follow symlinks to ensure the underlying .so files
		# get installed, not symlinks. The "preferred method" below is installing the symlink,
		# which produces unusable release archives.
		get_target_property(TBB_SO_FILE_SYMLINK TBB::tbb IMPORTED_LOCATION_RELEASE)
		message(STATUS "TBB .so symlink: ${TBB_SO_FILE_SYMLINK}")

		# just the name part of the symlink
		get_filename_component(TBB_SO_FILE_SYMLINK_NAME "${TBB_SO_FILE_SYMLINK}" NAME)
		message(STATUS "TBB .so symlink name: ${TBB_SO_FILE_SYMLINK_NAME}")

		get_filename_component(TBB_SO_FILE "${TBB_SO_FILE_SYMLINK}" REALPATH)
		message(STATUS "TBB .so file: ${TBB_SO_FILE}")

		install(FILES ${TBB_SO_FILE} DESTINATION bin RENAME "${TBB_SO_FILE_SYMLINK_NAME}")

		# tbbmalloc
		get_target_property(TBBMALLOC_SO_FILE_SYMLINK TBB::tbbmalloc IMPORTED_LOCATION_RELEASE)
		message(STATUS "TBBMALLOC .so symlink: ${TBBMALLOC_SO_FILE_SYMLINK}")

		get_filename_component(TBBMALLOC_SO_FILE_SYMLINK_NAME "${TBBMALLOC_SO_FILE_SYMLINK}" NAME)
		message(STATUS "TBBMALLOC .so symlink name: ${TBBMALLOC_SO_FILE_SYMLINK_NAME}")

		get_filename_component(TBBMALLOC_SO_FILE "${TBBMALLOC_SO_FILE_SYMLINK}" REALPATH)

		message(STATUS "TBBMALLOC .so file: ${TBBMALLOC_SO_FILE}")

		install(FILES ${TBBMALLOC_SO_FILE} DESTINATION bin RENAME "${TBBMALLOC_SO_FILE_SYMLINK_NAME}")
	else()
		# preferred method
		install(FILES $<TARGET_FILE:TBB::tbb> DESTINATION bin)
		install(FILES $<TARGET_FILE:TBB::tbbmalloc> DESTINATION bin)
	endif()

	if((NOT SKIP_EMBREE_INSTALL) AND (NOT EMBREE_LICENSE STREQUAL EMBREE_LICENSE-NOTFOUND))
		install(FILES ${EMBREE_LICENSE} DESTINATION bin RENAME LICENSE-embree.txt)
	endif()
endif(embree_FOUND)

copy_mingw_dlls(light)

install(TARGETS light RUNTIME DESTINATION bin)
install(FILES ../gpl_v3.txt DESTINATION bin)
