set(BSPUTIL_SOURCES
	bsputil.cc
	../include/bsputil/bsputil.hh
)

add_library(libbsputil STATIC ${BSPUTIL_SOURCES})
target_link_libraries(libbsputil common TBB::tbb TBB::tbbmalloc fmt::fmt)

add_executable(bsputil main.cc)
target_link_libraries(bsputil libbsputil)

# HACK: copy .dll dependencies
add_custom_command(TARGET bsputil POST_BUILD
                   COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:TBB::tbb>" "$<TARGET_FILE_DIR:bsputil>"
				   COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:TBB::tbbmalloc>" "$<TARGET_FILE_DIR:bsputil>"
				   )
copy_mingw_dlls(bsputil)

install(TARGETS bsputil RUNTIME DESTINATION bin)
