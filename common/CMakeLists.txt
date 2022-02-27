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
    ${CMAKE_SOURCE_DIR}/common/fs.cc
    ${CMAKE_SOURCE_DIR}/common/imglib.cc
    ${CMAKE_SOURCE_DIR}/common/settings.cc
    ${CMAKE_SOURCE_DIR}/common/debugger.natvis
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
    ${CMAKE_SOURCE_DIR}/include/common/threads.hh
    ${CMAKE_SOURCE_DIR}/include/common/fs.hh
    ${CMAKE_SOURCE_DIR}/include/common/imglib.hh
	${CMAKE_SOURCE_DIR}/include/common/settings.hh)

target_link_libraries(common ${CMAKE_THREAD_LIBS_INIT} TBB::tbb fmt::fmt nlohmann_json::nlohmann_json)

# test (copied from light/CMakeLists.txt)

set(COMMON_TEST_SOURCE 
	test.cc)

add_executable(testcommon EXCLUDE_FROM_ALL ${COMMON_TEST_SOURCE})
add_test(testcommon testcommon)

# HACK: copy .dll dependencies
add_custom_command(TARGET testcommon POST_BUILD
                   COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:TBB::tbb>" "$<TARGET_FILE_DIR:testcommon>")

target_link_libraries (testcommon common ${CMAKE_THREAD_LIBS_INIT} TBB::tbb gtest fmt::fmt)