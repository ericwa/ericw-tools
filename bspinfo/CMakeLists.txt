cmake_minimum_required (VERSION 2.6)
project (bspinfo)

set(BSPINFO_SOURCES
	bspinfo.c
	${CMAKE_SOURCE_DIR}/common/cmdlib.c
	${CMAKE_SOURCE_DIR}/common/bspfile.c
	${CMAKE_SOURCE_DIR}/common/log.c
	${CMAKE_SOURCE_DIR}/common/threads.c
	${COMMON_INCLUDES})

add_executable(bspinfo ${BSPINFO_SOURCES})
target_link_libraries(bspinfo ${CMAKE_THREAD_LIBS_INIT})
install(TARGETS bspinfo RUNTIME DESTINATION bin)
