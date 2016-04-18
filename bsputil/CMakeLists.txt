cmake_minimum_required (VERSION 2.6)
project (bsputil)

set(BSPUTIL_SOURCES
	bsputil.c
	${CMAKE_SOURCE_DIR}/common/cmdlib.c
	${CMAKE_SOURCE_DIR}/common/bspfile.c
	${CMAKE_SOURCE_DIR}/common/log.c
	${CMAKE_SOURCE_DIR}/common/threads.c
	${COMMON_INCLUDES})

add_executable(bsputil ${BSPUTIL_SOURCES})
target_link_libraries(bsputil ${CMAKE_THREAD_LIBS_INIT})
set_property(TARGET bsputil PROPERTY C_STANDARD 99)
install(TARGETS bsputil RUNTIME DESTINATION bin)
