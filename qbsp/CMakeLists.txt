cmake_minimum_required (VERSION 2.8)
project (qbsp CXX)

add_definitions(-DDOUBLEVEC_T)

add_executable(qbsp ${QBSP_SOURCES} main.cc)
target_link_libraries(qbsp ${CMAKE_THREAD_LIBS_INIT})
install(TARGETS qbsp RUNTIME DESTINATION bin)
