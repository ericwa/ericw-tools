cmake_minimum_required (VERSION 2.8)
project (bsputil CXX)

set(BSPUTIL_SOURCES
	bsputil.cc
	${CMAKE_SOURCE_DIR}/common/cmdlib.cc
	${CMAKE_SOURCE_DIR}/common/bspfile.cc
	${CMAKE_SOURCE_DIR}/common/log.cc
	${CMAKE_SOURCE_DIR}/common/threads.cc
	${COMMON_INCLUDES})

add_executable(bsputil ${BSPUTIL_SOURCES})
target_link_libraries(bsputil ${CMAKE_THREAD_LIBS_INIT})
install(TARGETS bsputil RUNTIME DESTINATION bin)
