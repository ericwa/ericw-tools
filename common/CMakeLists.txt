add_library(common STATIC 
    ${CMAKE_SOURCE_DIR}/common/bspinfo.cc
    ${CMAKE_SOURCE_DIR}/common/bspfile.cc
    ${CMAKE_SOURCE_DIR}/common/bsputils.cc
    ${CMAKE_SOURCE_DIR}/common/cmdlib.cc
    ${CMAKE_SOURCE_DIR}/common/entdata.cc
    ${CMAKE_SOURCE_DIR}/common/log.cc
    ${CMAKE_SOURCE_DIR}/common/mathlib.cc
    ${CMAKE_SOURCE_DIR}/common/parser.cc
    ${CMAKE_SOURCE_DIR}/common/qvec.cc
    ${CMAKE_SOURCE_DIR}/common/threads.cc
    ${CMAKE_SOURCE_DIR}/common/fs.cc
    ${CMAKE_SOURCE_DIR}/common/imglib.cc
    ${CMAKE_SOURCE_DIR}/common/settings.cc
    ${CMAKE_SOURCE_DIR}/common/prtfile.cc
    ${CMAKE_SOURCE_DIR}/common/debugger.natvis
    ${CMAKE_SOURCE_DIR}/include/common/aabb.hh
    ${CMAKE_SOURCE_DIR}/include/common/bitflags.hh
    ${CMAKE_SOURCE_DIR}/include/common/bspinfo.hh
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
    ${CMAKE_SOURCE_DIR}/include/common/parallel.hh
    ${CMAKE_SOURCE_DIR}/include/common/threads.hh
    ${CMAKE_SOURCE_DIR}/include/common/fs.hh
    ${CMAKE_SOURCE_DIR}/include/common/imglib.hh
    ${CMAKE_SOURCE_DIR}/include/common/settings.hh
    ${CMAKE_SOURCE_DIR}/include/common/prtfile.hh
    ${CMAKE_SOURCE_DIR}/include/common/vectorutils.hh)

target_link_libraries(common ${CMAKE_THREAD_LIBS_INIT} TBB::tbb fmt::fmt nlohmann_json::nlohmann_json)
