add_subdirectory(fmt EXCLUDE_FROM_ALL)

set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
add_subdirectory(googletest EXCLUDE_FROM_ALL)
