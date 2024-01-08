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

# so the executable will search for dylib's in the same directory as the executable
if(APPLE)
    add_custom_command(TARGET lightpreview POST_BUILD
        COMMAND bash ARGS -c \"install_name_tool -add_rpath @loader_path $<TARGET_FILE:lightpreview> || true\")
endif()

# Install Qt DLL's
if (WIN32)
    install(FILES $<TARGET_FILE:Qt5::Widgets> DESTINATION bin)
    install(FILES $<TARGET_FILE:Qt5::Gui> DESTINATION bin)
    install(FILES $<TARGET_FILE:Qt5::Core> DESTINATION bin)
    install(FILES "$<TARGET_FILE:Qt5::QWindowsIntegrationPlugin>" DESTINATION bin/platforms)
endif ()

function(apple_install_lib SRC_TARGET_NAME DEST_PATH)
    # SRC_TARGET_NAME - a target name, e.g. Qt5::Widgets
    # DEST_PATH - destination path to install the library to
    
    get_target_property(SO_FILE_SYMLINK ${SRC_TARGET_NAME} IMPORTED_LOCATION_RELEASE)
    message(STATUS "${SRC_TARGET_NAME} .so symlink: ${SO_FILE_SYMLINK}")

    get_filename_component(SO_FILE_SYMLINK_NAME "${SO_FILE_SYMLINK}" NAME)
    message(STATUS "${SRC_TARGET_NAME} .so symlink name: ${SO_FILE_SYMLINK_NAME}")

    get_filename_component(SO_FILE "${SO_FILE_SYMLINK}" REALPATH)

    message(STATUS "${SRC_TARGET_NAME} .so file: ${SO_FILE}, renaming to: ${SO_FILE_SYMLINK_NAME} and installing in: ${DEST_PATH}")

    install(FILES ${SO_FILE} DESTINATION "${DEST_PATH}" RENAME "${SO_FILE_SYMLINK_NAME}")
endfunction()

if (APPLE)
    apple_install_lib(Qt5::Widgets "bin/QtWidgets.framework/Versions/5")
    apple_install_lib(Qt5::Gui "bin/QtGui.framework/Versions/5")
    apple_install_lib(Qt5::Core "bin/QtCore.framework/Versions/5")
    apple_install_lib(Qt5::QCocoaIntegrationPlugin "bin/platforms")
    # these are required by QCocoaIntegrationPlugin
    apple_install_lib(Qt5::PrintSupport "bin/QtPrintSupport.framework/Versions/5")
    apple_install_lib(Qt5::DBus "bin/QtDBus.framework/Versions/5")
endif ()

install(TARGETS lightpreview RUNTIME DESTINATION bin)
