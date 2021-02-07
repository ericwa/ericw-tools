set(BSPUTIL_SOURCES
	bsputil.cc
	decompile.h
	decompile.cpp
	${CMAKE_SOURCE_DIR}/common/cmdlib.cc
	${CMAKE_SOURCE_DIR}/common/bspfile.cc
	${CMAKE_SOURCE_DIR}/common/bsputils.cc
	${CMAKE_SOURCE_DIR}/common/entdata.cc
	${CMAKE_SOURCE_DIR}/common/mathlib.cc
	${CMAKE_SOURCE_DIR}/common/qvec.cc
	${CMAKE_SOURCE_DIR}/common/polylib.cc
	${CMAKE_SOURCE_DIR}/common/log.cc
	${CMAKE_SOURCE_DIR}/common/threads.cc
	${COMMON_INCLUDES})

add_executable(bsputil ${BSPUTIL_SOURCES})
target_link_libraries(bsputil ${CMAKE_THREAD_LIBS_INIT} TBB::tbb fmt::fmt)
install(TARGETS bsputil RUNTIME DESTINATION bin)
