enable_testing()

add_executable(tests
		test.cc
		test_main.cc
		test_common.cc
		test_entities.cc
		test_light.cc
		test_ltface.cc
		test_qbsp.cc
		test_qbsp.hh
		test_qbsp_q2.cc
		test_vis.cc
		testutils.hh
		${CMAKE_CURRENT_BINARY_DIR}/../testmaps.hh
		testutils.hh
		benchmark.cc
		test_bsputil.cc)

find_package(embree 3.0 REQUIRED)
INCLUDE_DIRECTORIES(${EMBREE_INCLUDE_DIRS})

# HACK: Windows embree .dll's from https://github.com/embree/embree/releases ship with a tbb12.dll
# and we need to copy it from the embree/bin directory to our light.exe/testlight.exe dir in order for them to run
find_file(EMBREE_TBB_DLL tbb12.dll
			"${EMBREE_ROOT_DIR}/bin"
			NO_DEFAULT_PATH)
if (NOT EMBREE_TBB_DLL STREQUAL EMBREE_TBB_DLL-NOTFOUND)
	message(STATUS "Found embree EMBREE_TBB_DLL: ${EMBREE_TBB_DLL}")
endif()

target_link_libraries(tests libqbsp liblight libvis libbsputil common TBB::tbb TBB::tbbmalloc doctest::doctest fmt::fmt nanobench::nanobench)

target_compile_definitions(tests PRIVATE DOCTEST_CONFIG_SUPER_FAST_ASSERTS)

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

add_definitions(-DHAVE_EMBREE)
