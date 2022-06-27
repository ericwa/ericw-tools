set(QBSP_INCLUDES
	${CMAKE_SOURCE_DIR}/include/qbsp/qbsp.hh
	${CMAKE_SOURCE_DIR}/include/qbsp/brush.hh
	${CMAKE_SOURCE_DIR}/include/qbsp/csg4.hh
	${CMAKE_SOURCE_DIR}/include/qbsp/map.hh
	${CMAKE_SOURCE_DIR}/include/qbsp/winding.hh
	${CMAKE_SOURCE_DIR}/include/qbsp/merge.hh
	${CMAKE_SOURCE_DIR}/include/qbsp/outside.hh
	${CMAKE_SOURCE_DIR}/include/qbsp/portals.hh
	${CMAKE_SOURCE_DIR}/include/qbsp/solidbsp.hh
	${CMAKE_SOURCE_DIR}/include/qbsp/surfaces.hh
	${CMAKE_SOURCE_DIR}/include/qbsp/writebsp.hh)

set(QBSP_SOURCES
	${CMAKE_SOURCE_DIR}/qbsp/brush.cc
	${CMAKE_SOURCE_DIR}/qbsp/csg4.cc
	${CMAKE_SOURCE_DIR}/qbsp/map.cc
	${CMAKE_SOURCE_DIR}/qbsp/merge.cc
	${CMAKE_SOURCE_DIR}/qbsp/outside.cc
	${CMAKE_SOURCE_DIR}/qbsp/portals.cc
	${CMAKE_SOURCE_DIR}/qbsp/qbsp.cc
	${CMAKE_SOURCE_DIR}/qbsp/solidbsp.cc
	${CMAKE_SOURCE_DIR}/qbsp/surfaces.cc
	${CMAKE_SOURCE_DIR}/qbsp/tjunc.cc
	${CMAKE_SOURCE_DIR}/qbsp/writebsp.cc
	${CMAKE_SOURCE_DIR}/qbsp/exportobj.cc
	${QBSP_INCLUDES})

add_library(libqbsp STATIC ${QBSP_SOURCES})
target_link_libraries(libqbsp common ${CMAKE_THREAD_LIBS_INIT} TBB::tbb fmt::fmt nlohmann_json::nlohmann_json)

add_executable(qbsp main.cc)
target_link_libraries(qbsp libqbsp)

install(TARGETS qbsp RUNTIME DESTINATION bin)

# HACK: copy .dll dependencies
add_custom_command(TARGET qbsp POST_BUILD
                   COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:TBB::tbb>" "$<TARGET_FILE_DIR:qbsp>")

# test (see: https://google.github.io/googletest/quickstart-cmake.html)

enable_testing()

set(QBSP_TEST_SOURCE
	test.cc
	test_qbsp.cc
	${CMAKE_BINARY_DIR}/testmaps.hh)
add_executable(testqbsp ${QBSP_TEST_SOURCE})
target_link_libraries (testqbsp libqbsp common ${CMAKE_THREAD_LIBS_INIT} TBB::tbb Catch2::Catch2 fmt::fmt nanobench::nanobench)

# HACK: copy .dll dependencies
add_custom_command(TARGET testqbsp POST_BUILD
                   COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:TBB::tbb>" "$<TARGET_FILE_DIR:testqbsp>")

include(Catch)
catch_discover_tests(testqbsp)