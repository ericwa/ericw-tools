add_executable(bspinfo main.cc)
target_link_libraries(bspinfo common fmt::fmt)

# HACK: copy .dll dependencies
add_custom_command(TARGET bspinfo POST_BUILD
                   COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:TBB::tbb>" "$<TARGET_FILE_DIR:bspinfo>"
                   COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:TBB::tbbmalloc>" "$<TARGET_FILE_DIR:bspinfo>"
                   )
copy_mingw_dlls(bspinfo)

install(TARGETS bspinfo RUNTIME DESTINATION bin)
