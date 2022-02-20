set(BSPINFO_SOURCES
	bspinfo.cc)

add_executable(bspinfo ${BSPINFO_SOURCES})
target_link_libraries(bspinfo common fmt::fmt nlohmann_json::nlohmann_json)

# HACK: copy .dll dependencies
add_custom_command(TARGET bspinfo POST_BUILD
                   COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:TBB::tbb>" "$<TARGET_FILE_DIR:bspinfo>")

install(TARGETS bspinfo RUNTIME DESTINATION bin)
