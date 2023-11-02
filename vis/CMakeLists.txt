set(VIS_INCLUDES
	../include/vis/leafbits.hh
	../include/vis/vis.hh)

set(VIS_SOURCES
	flow.cc
	vis.cc
	soundpvs.cc
	state.cc
	${VIS_INCLUDES})

add_library(libvis STATIC ${VIS_SOURCES})
target_link_libraries(libvis PRIVATE common ${CMAKE_THREAD_LIBS_INIT} fmt::fmt)

# FIXME: still needed?
find_library(M_LIB m)
if (M_LIB)
    target_link_libraries(libvis PRIVATE ${M_LIB})
endif (M_LIB)

add_executable(vis main.cc)
target_link_libraries(vis PRIVATE common libvis)

# HACK: copy .dll dependencies
add_custom_command(TARGET vis POST_BUILD
                   COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:TBB::tbb>" "$<TARGET_FILE_DIR:vis>"
				   COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:TBB::tbbmalloc>" "$<TARGET_FILE_DIR:vis>"
				   )
copy_mingw_dlls(vis)

install(TARGETS vis RUNTIME DESTINATION bin)
