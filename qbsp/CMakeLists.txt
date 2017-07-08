cmake_minimum_required (VERSION 2.8)
project (qbsp CXX)

add_definitions(-DDOUBLEVEC_T)

add_executable(qbsp ${QBSP_SOURCES} main.cc)
target_link_libraries(qbsp ${CMAKE_THREAD_LIBS_INIT})
install(TARGETS qbsp RUNTIME DESTINATION bin)

# test (copied from light/CMakeLists.txt)

set(GOOGLETEST_SOURCES ${CMAKE_SOURCE_DIR}/3rdparty/googletest/src/gtest-all.cc)
include_directories(${CMAKE_SOURCE_DIR}/3rdparty/googletest/include)
include_directories(${CMAKE_SOURCE_DIR}/3rdparty/googletest)

set(QBSP_TEST_SOURCE 
	${QBSP_SOURCES}
	${GOOGLETEST_SOURCES}
	test.cc
	test_qbsp.cc)

add_executable(testqbsp EXCLUDE_FROM_ALL ${QBSP_TEST_SOURCE})
add_test(testqbsp testqbsp)

target_link_libraries (testqbsp ${CMAKE_THREAD_LIBS_INIT})
