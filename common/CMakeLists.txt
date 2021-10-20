add_library(common STATIC 
    ${CMAKE_SOURCE_DIR}/common/bspfile.cc
    ${CMAKE_SOURCE_DIR}/common/bsputils.cc
    ${CMAKE_SOURCE_DIR}/common/cmdlib.cc
    ${CMAKE_SOURCE_DIR}/common/entdata.cc
    ${CMAKE_SOURCE_DIR}/common/log.cc
    ${CMAKE_SOURCE_DIR}/common/mathlib.cc
    ${CMAKE_SOURCE_DIR}/common/parser.cc
    ${CMAKE_SOURCE_DIR}/common/qvec.cc
    ${CMAKE_SOURCE_DIR}/common/threads.cc
    ${CMAKE_SOURCE_DIR}/include/common/aabb.hh
    ${CMAKE_SOURCE_DIR}/include/common/bspfile.hh
    ${CMAKE_SOURCE_DIR}/include/common/bsputils.hh
    ${CMAKE_SOURCE_DIR}/include/common/cmdlib.hh
    ${CMAKE_SOURCE_DIR}/include/common/entdata.h
    ${CMAKE_SOURCE_DIR}/include/common/log.hh
    ${CMAKE_SOURCE_DIR}/include/common/mathlib.hh
    ${CMAKE_SOURCE_DIR}/include/common/parser.hh
    ${CMAKE_SOURCE_DIR}/include/common/polylib.hh
    ${CMAKE_SOURCE_DIR}/include/common/qvec.hh
    ${CMAKE_SOURCE_DIR}/include/common/json.hh
    ${CMAKE_SOURCE_DIR}/include/common/threads.hh)

target_link_libraries(common ${CMAKE_THREAD_LIBS_INIT} fmt::fmt nlohmann_json::nlohmann_json)
