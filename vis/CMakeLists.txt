project (vis CXX)

set(VIS_INCLUDES
	${CMAKE_SOURCE_DIR}/include/vis/leafbits.hh
	${CMAKE_SOURCE_DIR}/include/vis/vis.hh)

set(VIS_SOURCES
	flow.cc
	vis.cc
	soundpvs.cc
	state.cc
	${VIS_INCLUDES})

add_executable(vis ${VIS_SOURCES})
target_link_libraries (vis common ${CMAKE_THREAD_LIBS_INIT} fmt::fmt)
find_library(M_LIB m)
if (M_LIB)
    target_link_libraries (vis ${M_LIB})
endif (M_LIB)

# HACK: copy .dll dependencies
add_custom_command(TARGET vis POST_BUILD
                   COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:TBB::tbb>" "$<TARGET_FILE_DIR:vis>")

install(TARGETS vis RUNTIME DESTINATION bin)
