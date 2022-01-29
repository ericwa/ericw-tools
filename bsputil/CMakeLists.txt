set(BSPUTIL_SOURCES
	bsputil.cc
	decompile.h
	decompile.cpp)

add_executable(bsputil ${BSPUTIL_SOURCES})
target_link_libraries(bsputil common TBB::tbb fmt::fmt)

# HACK: copy .dll dependencies
add_custom_command(TARGET bsputil POST_BUILD
                   COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:TBB::tbb>" "$<TARGET_FILE_DIR:bsputil>")

install(TARGETS bsputil RUNTIME DESTINATION bin)
