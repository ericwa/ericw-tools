set(VIS_INCLUDES
	${CMAKE_SOURCE_DIR}/include/vis/leafbits.hh
	${CMAKE_SOURCE_DIR}/include/vis/vis.hh)

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
                   COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:TBB::tbb>" "$<TARGET_FILE_DIR:vis>")

install(TARGETS vis RUNTIME DESTINATION bin)

# test

enable_testing()

add_executable(testvis
		test.cc
		test_vis.cc)
target_link_libraries(testvis PRIVATE libvis common Catch2::Catch2 fmt::fmt)

# HACK: copy .dll dependencies
add_custom_command(TARGET testvis POST_BUILD
                   COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:TBB::tbb>" "$<TARGET_FILE_DIR:testvis>")

include(Catch)
catch_discover_tests(testvis)
