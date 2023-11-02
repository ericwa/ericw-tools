set(MAPUTIL_SOURCES
	maputil.cc
	../include/maputil/maputil.hh
)

find_package(Lua)

add_library(libmaputil STATIC ${MAPUTIL_SOURCES})

target_link_libraries(libmaputil common TBB::tbb TBB::tbbmalloc fmt::fmt)

if (LUA_LIBRARIES)
	target_link_libraries(libmaputil ${LUA_LIBRARIES})
	target_include_directories(libmaputil PRIVATE ${LUA_INCLUDE_DIR})
endif()

add_executable(maputil main.cc)
target_link_libraries(maputil libmaputil)

if (LUA_LIBRARIES)
	target_include_directories(maputil PRIVATE ${LUA_INCLUDE_DIR})
	add_definitions(-DUSE_LUA)
endif()

# HACK: copy .dll dependencies
add_custom_command(TARGET maputil POST_BUILD
                   COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:TBB::tbb>" "$<TARGET_FILE_DIR:maputil>"
				   COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:TBB::tbbmalloc>" "$<TARGET_FILE_DIR:maputil>"
				   )
copy_mingw_dlls(maputil)

install(TARGETS maputil RUNTIME DESTINATION bin)
