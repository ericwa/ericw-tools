cmake_minimum_required (VERSION 2.8)
project (qbsp3 CXX)

add_definitions(-DDOUBLEVEC_T)

if (UNIX)
add_definitions(-DLINUX)
endif (UNIX)

include_directories("${CMAKE_SOURCE_DIR}/../include")

set(QBSP3_HEADERS
    ../include/common/bspfile.hh
    ../include/common/cmdlib.hh
    ../include/common/mathlib.hh
    ../include/common/polylib.hh
    ../include/common/threads.hh
    qbsp.h)

set(QBSP3_SOURCES
    brushbsp.cc
    faces.cc
    nodraw.cc
    glfile.cc
    leakfile.cc
    map.cc
    portals.cc
    prtfile.cc
    qbsp3.cc
    tree.cc
    textures.cc
    writebsp.cc
    csg.cc
    ../common/cmdlib.cc
    ../common/mathlib.cc
    ../common/polylib.cc
    ../common/threads.cc
    ../common/bspfile.cc)

find_package (Threads)

# set our C/C++ dialects
if (CMAKE_VERSION VERSION_LESS "3.1")
	set (CMAKE_CXX_FLAGS "-std=gnu++11 ${CMAKE_CXX_FLAGS}")
	set (CMAKE_C_FLAGS "-std=gnu11 ${CMAKE_C_FLAGS}")
else ()
	set(CMAKE_CXX_STANDARD 11)
	set(CMAKE_CXX_STANDARD_REQUIRED ON)
	set(CMAKE_C_STANDARD 99)
endif ()

add_executable(qbsp3 ${QBSP3_SOURCES} ${QBSP3_HEADERS})
target_link_libraries(qbsp3 ${CMAKE_THREAD_LIBS_INIT})

install(TARGETS qbsp3 RUNTIME DESTINATION bin)
