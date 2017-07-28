cmake_minimum_required (VERSION 2.8)
project (qbsp3 C)

add_definitions(-DDOUBLEVEC_T)

include_directories("${CMAKE_SOURCE_DIR}/common")

set(QBSP3_SOURCES
    brushbsp.c
    faces.c
    nodraw.c
    glfile.c
    leakfile.c
    map.c
    portals.c
    prtfile.c
    qbsp3.c
    tree.c
    textures.c
    writebsp.c
    csg.c
    common/cmdlib.c
    common/mathlib.c
    common/polylib.c
    common/scriplib.c
    common/threads.c
    common/bspfile.c)

find_package (Threads)

add_executable(qbsp3 ${QBSP3_SOURCES})
target_link_libraries(qbsp3 ${CMAKE_THREAD_LIBS_INIT})

install(TARGETS qbsp3 RUNTIME DESTINATION bin)
