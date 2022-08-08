add_subdirectory(fmt EXCLUDE_FROM_ALL)
add_subdirectory(Catch2 EXCLUDE_FROM_ALL)
add_subdirectory(json EXCLUDE_FROM_ALL)
add_subdirectory(nanobench EXCLUDE_FROM_ALL)

set(BUILD_WITH_PEDANTIC_WARNINGS OFF CACHE BOOL "prevent pareto from adding /WX" FORCE)
add_subdirectory(pareto EXCLUDE_FROM_ALL)
