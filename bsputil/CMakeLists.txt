set(BSPUTIL_SOURCES
	bsputil.cc
	decompile.h
	decompile.cpp)

add_executable(bsputil ${BSPUTIL_SOURCES})
target_link_libraries(bsputil common TBB::tbb fmt::fmt)
install(TARGETS bsputil RUNTIME DESTINATION bin)
