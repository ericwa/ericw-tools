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
    glview.h
        stats.cpp
        stats.h)

set_target_properties(lightpreview PROPERTIES WIN32_EXECUTABLE YES)
set_target_properties(lightpreview PROPERTIES MACOSX_BUNDLE TRUE)

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
if (WIN32)
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
endif()

copy_mingw_dlls(lightpreview)
add_loader_path_to_rpath(lightpreview)

# Install Qt DLL's
if (WIN32)
    install(FILES $<TARGET_FILE:Qt5::Widgets> DESTINATION .)
    install(FILES $<TARGET_FILE:Qt5::Gui> DESTINATION .)
    install(FILES $<TARGET_FILE:Qt5::Core> DESTINATION .)
    install(FILES "$<TARGET_FILE:Qt5::QWindowsIntegrationPlugin>" DESTINATION platforms)
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
    apple_install_lib(TBB::tbb "$<TARGET_FILE_DIR:lightpreview>")
    apple_install_lib(TBB::tbbmalloc "$<TARGET_FILE_DIR:lightpreview>")
    apple_install_lib(embree "$<TARGET_FILE_DIR:lightpreview>")

    # TODO: this should be replaced with macdeployqt
    apple_install_lib(Qt5::Widgets "$<TARGET_FILE_DIR:lightpreview>/QtWidgets.framework/Versions/5")
    apple_install_lib(Qt5::Gui "$<TARGET_FILE_DIR:lightpreview>/QtGui.framework/Versions/5")
    apple_install_lib(Qt5::Core "$<TARGET_FILE_DIR:lightpreview>/QtCore.framework/Versions/5")
    apple_install_lib(Qt5::QCocoaIntegrationPlugin "$<TARGET_FILE_DIR:lightpreview>/platforms")

    # these are required by QCocoaIntegrationPlugin
    find_package(Qt5DBus REQUIRED)
    find_package(Qt5PrintSupport REQUIRED)
    apple_install_lib(Qt5::PrintSupport "$<TARGET_FILE_DIR:lightpreview>/QtPrintSupport.framework/Versions/5")
    apple_install_lib(Qt5::DBus "$<TARGET_FILE_DIR:lightpreview>/QtDBus.framework/Versions/5")
endif ()

install(TARGETS lightpreview RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} BUNDLE DESTINATION ${CMAKE_INSTALL_BINDIR})
