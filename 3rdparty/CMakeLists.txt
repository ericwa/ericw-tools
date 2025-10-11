add_subdirectory(fmt EXCLUDE_FROM_ALL)
add_subdirectory(nanobench EXCLUDE_FROM_ALL)

set(BUILD_WITH_PEDANTIC_WARNINGS OFF CACHE BOOL "prevent pareto from adding /WX" FORCE)
add_subdirectory(pareto EXCLUDE_FROM_ALL)

include(FetchContent)
FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG        f8d7d77c06936315286eb55f8de22cd23c188571 # v1.14.0
)
# For Windows: Prevent overriding the parent project's compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
set(INSTALL_GTEST FALSE CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

set(JSONCPP_WITH_TESTS OFF)
set(JSONCPP_WITH_POST_BUILD_UNITTEST OFF)
set(JSONCPP_WITH_WARNING_AS_ERROR OFF)
set(JSONCPP_WITH_STRICT_ISO OFF)
set(JSONCPP_WITH_PKGCONFIG_SUPPORT OFF)
set(JSONCPP_WITH_CMAKE_PACKAGE OFF)
set(JSONCPP_WITH_EXAMPLE OFF)
set(JSONCPP_STATIC_WINDOWS_RUNTIME OFF)
set(BUILD_SHARED_LIBS OFF)
set(BUILD_STATIC_LIBS ON)
set(BUILD_OBJECT_LIBS OFF)
add_subdirectory(jsoncpp EXCLUDE_FROM_ALL)
