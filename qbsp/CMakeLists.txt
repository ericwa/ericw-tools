set(QBSP_INCLUDES
	${CMAKE_SOURCE_DIR}/include/qbsp/file.hh
	${CMAKE_SOURCE_DIR}/include/qbsp/qbsp.hh
	${CMAKE_SOURCE_DIR}/include/qbsp/wad.hh
	${CMAKE_SOURCE_DIR}/include/qbsp/brush.hh
	${CMAKE_SOURCE_DIR}/include/qbsp/csg4.hh
	${CMAKE_SOURCE_DIR}/include/qbsp/map.hh
	${CMAKE_SOURCE_DIR}/include/qbsp/winding.hh
	${CMAKE_SOURCE_DIR}/include/qbsp/merge.hh
	${CMAKE_SOURCE_DIR}/include/qbsp/outside.hh
	${CMAKE_SOURCE_DIR}/include/qbsp/portals.hh
	${CMAKE_SOURCE_DIR}/include/qbsp/region.hh
	${CMAKE_SOURCE_DIR}/include/qbsp/solidbsp.hh
	${CMAKE_SOURCE_DIR}/include/qbsp/surfaces.hh
	${CMAKE_SOURCE_DIR}/include/qbsp/tjunc.hh
	${CMAKE_SOURCE_DIR}/include/qbsp/writebsp.hh)

set(QBSP_SOURCES
	${CMAKE_SOURCE_DIR}/qbsp/brush.cc
	${CMAKE_SOURCE_DIR}/qbsp/csg4.cc
	${CMAKE_SOURCE_DIR}/qbsp/file.cc
	${CMAKE_SOURCE_DIR}/qbsp/globals.cc
	${CMAKE_SOURCE_DIR}/qbsp/map.cc
	${CMAKE_SOURCE_DIR}/qbsp/merge.cc
	${CMAKE_SOURCE_DIR}/qbsp/outside.cc
	${CMAKE_SOURCE_DIR}/qbsp/portals.cc
	${CMAKE_SOURCE_DIR}/qbsp/qbsp.cc
	${CMAKE_SOURCE_DIR}/qbsp/solidbsp.cc
	${CMAKE_SOURCE_DIR}/qbsp/surfaces.cc
	${CMAKE_SOURCE_DIR}/qbsp/tjunc.cc
	${CMAKE_SOURCE_DIR}/qbsp/wad.cc
	${CMAKE_SOURCE_DIR}/qbsp/writebsp.cc
	${CMAKE_SOURCE_DIR}/qbsp/exportobj.cc
	${QBSP_INCLUDES})

add_executable(qbsp ${QBSP_SOURCES} main.cc)
target_link_libraries(qbsp common ${CMAKE_THREAD_LIBS_INIT} TBB::tbb fmt::fmt)
install(TARGETS qbsp RUNTIME DESTINATION bin)

# test (copied from light/CMakeLists.txt)

set(QBSP_TEST_SOURCE 
	${QBSP_SOURCES}
	test.cc
	test_qbsp.cc)

add_executable(testqbsp EXCLUDE_FROM_ALL ${QBSP_TEST_SOURCE})
add_test(testqbsp testqbsp)

target_link_libraries (testqbsp common ${CMAKE_THREAD_LIBS_INIT} TBB::tbb gtest fmt::fmt)