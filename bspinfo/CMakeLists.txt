set(BSPINFO_SOURCES
	bspinfo.cc)

add_executable(bspinfo ${BSPINFO_SOURCES})
target_link_libraries(bspinfo common fmt::fmt nlohmann_json::nlohmann_json)
install(TARGETS bspinfo RUNTIME DESTINATION bin)
