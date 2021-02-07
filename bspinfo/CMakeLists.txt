set(BSPINFO_SOURCES
	bspinfo.cc
	${CMAKE_SOURCE_DIR}/common/entdata.cc
	${CMAKE_SOURCE_DIR}/common/cmdlib.cc
	${CMAKE_SOURCE_DIR}/common/bspfile.cc
	${CMAKE_SOURCE_DIR}/common/log.cc
	${CMAKE_SOURCE_DIR}/common/threads.cc
	${COMMON_INCLUDES})

add_executable(bspinfo ${BSPINFO_SOURCES})
target_link_libraries(bspinfo ${CMAKE_THREAD_LIBS_INIT})
install(TARGETS bspinfo RUNTIME DESTINATION bin)
