add_executable(tests
        test_settings.cc
		test_main.cc
		test_common.cc
		test_entities.cc
		test_light.cc
		test_ltface.cc
		test_spatialindex.cpp
		test_qbsp.cc
		test_qbsp.hh
		test_qbsp_q2.cc
		test_vis.cc
		${CMAKE_CURRENT_BINARY_DIR}/../testmaps.hh
		benchmark.cc
		test_bsputil.cc
		test_main.hh)

INCLUDE_DIRECTORIES(${EMBREE_INCLUDE_DIRS})

# HACK: Windows embree .dll's from https://github.com/embree/embree/releases ship with a tbb12.dll
# and we need to copy it from the embree/bin directory to our light.exe/testlight.exe dir in order for them to run
find_file(EMBREE_TBB_DLL tbb12.dll
			"${EMBREE_ROOT_DIR}/bin"
			NO_DEFAULT_PATH)
if (NOT EMBREE_TBB_DLL STREQUAL EMBREE_TBB_DLL-NOTFOUND)
	message(STATUS "Found embree EMBREE_TBB_DLL: ${EMBREE_TBB_DLL}")
endif()

target_link_libraries(tests libqbsp liblight libvis libbsputil common TBB::tbb TBB::tbbmalloc GTest::gtest GTest::gmock fmt::fmt nanobench::nanobench)

# HACK: copy .dll dependencies
add_custom_command(TARGET tests POST_BUILD
					COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:embree>"   "$<TARGET_FILE_DIR:tests>"
					COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:TBB::tbb>" "$<TARGET_FILE_DIR:tests>"
					COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:TBB::tbbmalloc>" "$<TARGET_FILE_DIR:tests>"
					)
if (NOT EMBREE_TBB_DLL STREQUAL EMBREE_TBB_DLL-NOTFOUND)
	add_custom_command(TARGET tests POST_BUILD
					   COMMAND ${CMAKE_COMMAND} -E copy_if_different "${EMBREE_TBB_DLL}" "$<TARGET_FILE_DIR:tests>")
endif()
copy_mingw_dlls(tests)
add_loader_path_to_rpath(tests)

add_definitions(-DHAVE_EMBREE)
