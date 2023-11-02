project (lightpreview CXX)

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt5Widgets)
if(Qt5Widgets_FOUND)
else()
    message(WARNING "Qt5 not found, disabling lightpreview")
    return()
endif()

add_executable(lightpreview
    main.cpp
    mainwindow.cpp
    mainwindow.h
    glview.cpp
    glview.h)

set_target_properties(lightpreview PROPERTIES WIN32_EXECUTABLE YES)

find_package(embree 3.0 REQUIRED)
INCLUDE_DIRECTORIES(${EMBREE_INCLUDE_DIRS})

# HACK: Windows embree .dll's from https://github.com/embree/embree/releases ship with a tbb12.dll
# and we need to copy it from the embree/bin directory to our light.exe/testlight.exe dir in order for them to run
find_file(EMBREE_TBB_DLL tbb12.dll
        "${EMBREE_ROOT_DIR}/bin"
        NO_DEFAULT_PATH)
if (NOT EMBREE_TBB_DLL STREQUAL EMBREE_TBB_DLL-NOTFOUND)
    message(STATUS "Found embree EMBREE_TBB_DLL: ${EMBREE_TBB_DLL}")
endif()

target_link_libraries(lightpreview
        Qt5::Widgets
        libqbsp
        liblight
        libvis
        common
        TBB::tbb
        TBB::tbbmalloc
        fmt::fmt)

# from: http://stackoverflow.com/questions/40564443/copying-qt-dlls-to-executable-directory-on-windows-using-cmake
# Copy Qt DLL's to bin directory for debugging
add_custom_command(
    TARGET lightpreview POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:Qt5::Widgets> $<TARGET_FILE_DIR:lightpreview>
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:Qt5::Gui> $<TARGET_FILE_DIR:lightpreview>
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:Qt5::Core> $<TARGET_FILE_DIR:lightpreview>
)

# HACK: copy .dll dependencies
add_custom_command(TARGET lightpreview POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:embree>"   "$<TARGET_FILE_DIR:lightpreview>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:TBB::tbb>" "$<TARGET_FILE_DIR:lightpreview>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:TBB::tbbmalloc>" "$<TARGET_FILE_DIR:lightpreview>"
        )
if (NOT EMBREE_TBB_DLL STREQUAL EMBREE_TBB_DLL-NOTFOUND)
    add_custom_command(TARGET lightpreview POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${EMBREE_TBB_DLL}" "$<TARGET_FILE_DIR:lightpreview>")
endif()
copy_mingw_dlls(lightpreview)

# Install Qt DLL's
install(FILES $<TARGET_FILE:Qt5::Widgets> DESTINATION bin)
install(FILES $<TARGET_FILE:Qt5::Gui> DESTINATION bin)
install(FILES $<TARGET_FILE:Qt5::Core> DESTINATION bin)
if (WIN32)
    install(FILES "$<TARGET_FILE:Qt5::QWindowsIntegrationPlugin>" DESTINATION bin/platforms)
endif ()

install(TARGETS lightpreview RUNTIME DESTINATION bin)
