cmake_minimum_required (VERSION 3.0)
project (lightpreview CXX)

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt5Widgets)

add_executable(lightpreview
    main.cpp
    mainwindow.cpp
    mainwindow.h
    glview.cpp
    glview.h
    ${QBSP_SOURCES})

target_link_libraries(lightpreview Qt5::Widgets)

# from: http://stackoverflow.com/questions/40564443/copying-qt-dlls-to-executable-directory-on-windows-using-cmake
# Copy Qt DLL's to bin directory for debugging
add_custom_command(
    TARGET lightpreview POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:Qt5::Widgets> $<TARGET_FILE_DIR:lightpreview>
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:Qt5::Gui> $<TARGET_FILE_DIR:lightpreview>
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:Qt5::Core> $<TARGET_FILE_DIR:lightpreview>
)

# Install Qt DLL's
install(FILES $<TARGET_FILE:Qt5::Widgets> DESTINATION bin)
install(FILES $<TARGET_FILE:Qt5::Gui> DESTINATION bin)
install(FILES $<TARGET_FILE:Qt5::Core> DESTINATION bin)

install(TARGETS lightpreview RUNTIME DESTINATION bin)
