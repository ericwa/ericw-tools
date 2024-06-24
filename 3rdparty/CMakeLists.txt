add_subdirectory(fmt EXCLUDE_FROM_ALL)
add_subdirectory(json EXCLUDE_FROM_ALL)
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
FetchContent_MakeAvailable(googletest)
