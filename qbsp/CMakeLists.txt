set(QBSP_INCLUDES
	../include/qbsp/qbsp.hh
	../include/qbsp/brush.hh
	../include/qbsp/csg.hh
	../include/qbsp/exportobj.hh
	../include/qbsp/map.hh
	../include/qbsp/winding.hh
	../include/qbsp/merge.hh
	../include/qbsp/outside.hh
	../include/qbsp/portals.hh
	../include/qbsp/prtfile.hh
	../include/qbsp/brushbsp.hh
	../include/qbsp/faces.hh
	../include/qbsp/tjunc.hh
	../include/qbsp/tree.hh
	../include/qbsp/writebsp.hh)

set(QBSP_SOURCES
	brush.cc
	csg.cc
	map.cc
	merge.cc
	outside.cc
	portals.cc
	prtfile.cc
	qbsp.cc
	brushbsp.cc
	faces.cc
	tjunc.cc
	tree.cc
	writebsp.cc
	exportobj.cc
	${QBSP_INCLUDES})

add_library(libqbsp STATIC ${QBSP_SOURCES})
target_link_libraries(libqbsp common ${CMAKE_THREAD_LIBS_INIT} TBB::tbb TBB::tbbmalloc fmt::fmt nlohmann_json::nlohmann_json pareto)

add_executable(qbsp main.cc)
target_link_libraries(qbsp libqbsp)

install(TARGETS qbsp RUNTIME DESTINATION bin)

# HACK: copy .dll dependencies
add_custom_command(TARGET qbsp POST_BUILD
                   COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:TBB::tbb>" "$<TARGET_FILE_DIR:qbsp>"
				   COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:TBB::tbbmalloc>" "$<TARGET_FILE_DIR:qbsp>")

copy_mingw_dlls(qbsp)
