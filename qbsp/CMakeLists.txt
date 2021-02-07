add_definitions(-DDOUBLEVEC_T)

add_executable(qbsp ${QBSP_SOURCES} main.cc)
target_link_libraries(qbsp ${CMAKE_THREAD_LIBS_INIT} TBB::tbb)
install(TARGETS qbsp RUNTIME DESTINATION bin)

# test (copied from light/CMakeLists.txt)

set(QBSP_TEST_SOURCE 
	${QBSP_SOURCES}
	test.cc
	test_qbsp.cc)

add_executable(testqbsp EXCLUDE_FROM_ALL ${QBSP_TEST_SOURCE})
add_test(testqbsp testqbsp)

target_link_libraries (testqbsp ${CMAKE_THREAD_LIBS_INIT} TBB::tbb gtest)
