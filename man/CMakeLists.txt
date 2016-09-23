cmake_minimum_required (VERSION 2.8)
project (man)

find_program (GROFF groff)

if(GROFF)
    set(MANPAGES qbsp vis light bsputil bspinfo)
    foreach(MANPAGE ${MANPAGES})
        add_custom_command(
            OUTPUT ${MANPAGE}.html
            COMMAND ${GROFF} -Thtml -man ${CMAKE_SOURCE_DIR}/man/${MANPAGE}.1 > ${MANPAGE}.html
        )
        add_custom_target(man_${MANPAGE} DEPENDS ${MANPAGE}.html)
        install(FILES ${CMAKE_CURRENT_BINARY_DIR}/${MANPAGE}.html DESTINATION doc)
    endforeach()
    #make each of the main targets depend on the manuals
    add_dependencies(qbsp man_qbsp)
    add_dependencies(vis man_vis)
    add_dependencies(light man_light)
    add_dependencies(bsputil man_bsputil)
    add_dependencies(bspinfo man_bspinfo)
endif(GROFF)
