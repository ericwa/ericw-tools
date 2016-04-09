cmake_minimum_required (VERSION 2.6)
project (man)

find_program (GROFF NAMES groff)

if (GROFF-NOTFOUND)
    message(FATAL_ERROR "Could not find groff")
endif(GROFF-NOTFOUND)

if(GROFF)
    set(MANPAGES qbsp vis light bsputil bspinfo)
    foreach(MANPAGE ${MANPAGES})
        add_custom_command(
            OUTPUT ${MANPAGE}.txt
            COMMAND ${GROFF} -Tascii -man ${CMAKE_SOURCE_DIR}/man/${MANPAGE}.1 | col -bx > ${MANPAGE}.txt
        )
        add_custom_target(man_${MANPAGE} ALL DEPENDS ${MANPAGE}.txt)
        install(FILES ${MANPAGE}.txt DESTINATION doc)
    endforeach()
endif(GROFF)
