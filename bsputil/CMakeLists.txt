set(BSPUTIL_SOURCES
	bsputil.cc
)

add_executable(bsputil ${BSPUTIL_SOURCES})
target_link_libraries(bsputil common TBB::tbb TBB::tbbmalloc fmt::fmt)

# HACK: copy .dll dependencies
add_custom_command(TARGET bsputil POST_BUILD
                   COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:TBB::tbb>" "$<TARGET_FILE_DIR:bsputil>"
				   COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:TBB::tbbmalloc>" "$<TARGET_FILE_DIR:bsputil>"
				   )

install(TARGETS bsputil RUNTIME DESTINATION bin)
