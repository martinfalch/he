cmake_minimum_required(VERSION 3.22)

project(he)

add_executable(he
  src/main.c)
if(WIN32) # Also true for 64-bit Windows
  set_property(TARGET he PROPERTY
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  target_sources(he PRIVATE
    lib/PDCurses-3.9/pdcurses/addch.c
    lib/PDCurses-3.9/pdcurses/addchstr.c
    lib/PDCurses-3.9/pdcurses/addstr.c
    lib/PDCurses-3.9/pdcurses/attr.c
    lib/PDCurses-3.9/pdcurses/beep.c
    lib/PDCurses-3.9/pdcurses/bkgd.c
    lib/PDCurses-3.9/pdcurses/border.c
    lib/PDCurses-3.9/pdcurses/clear.c
    lib/PDCurses-3.9/pdcurses/color.c
    lib/PDCurses-3.9/pdcurses/delch.c
    lib/PDCurses-3.9/pdcurses/deleteln.c
    lib/PDCurses-3.9/pdcurses/getch.c
    lib/PDCurses-3.9/pdcurses/getstr.c
    lib/PDCurses-3.9/pdcurses/getyx.c
    lib/PDCurses-3.9/pdcurses/inch.c
    lib/PDCurses-3.9/pdcurses/inchstr.c
    lib/PDCurses-3.9/pdcurses/initscr.c
    lib/PDCurses-3.9/pdcurses/inopts.c
    lib/PDCurses-3.9/pdcurses/insch.c
    lib/PDCurses-3.9/pdcurses/insstr.c
    lib/PDCurses-3.9/pdcurses/instr.c
    lib/PDCurses-3.9/pdcurses/kernel.c
    lib/PDCurses-3.9/pdcurses/keyname.c
    lib/PDCurses-3.9/pdcurses/mouse.c
    lib/PDCurses-3.9/pdcurses/move.c
    lib/PDCurses-3.9/pdcurses/outopts.c
    lib/PDCurses-3.9/pdcurses/overlay.c
    lib/PDCurses-3.9/pdcurses/pad.c
    lib/PDCurses-3.9/pdcurses/panel.c
    lib/PDCurses-3.9/pdcurses/printw.c
    lib/PDCurses-3.9/pdcurses/refresh.c
    lib/PDCurses-3.9/pdcurses/scanw.c
    lib/PDCurses-3.9/pdcurses/scr_dump.c
    lib/PDCurses-3.9/pdcurses/scroll.c
    lib/PDCurses-3.9/pdcurses/slk.c
    lib/PDCurses-3.9/pdcurses/termattr.c
    lib/PDCurses-3.9/pdcurses/touch.c
    lib/PDCurses-3.9/pdcurses/util.c
    lib/PDCurses-3.9/pdcurses/window.c
    lib/PDCurses-3.9/pdcurses/debug.c

    lib/PDCurses-3.9/wincon/pdcclip.c
    lib/PDCurses-3.9/wincon/pdcdisp.c
    lib/PDCurses-3.9/wincon/pdcgetsc.c
    lib/PDCurses-3.9/wincon/pdckbd.c
    lib/PDCurses-3.9/wincon/pdcscrn.c
    lib/PDCurses-3.9/wincon/pdcsetsc.c
    lib/PDCurses-3.9/wincon/pdcutil.c
    )
  target_include_directories(he PRIVATE lib/PDCurses-3.9/)
else()
  find_package(Curses)
  target_link_libraries(he ${CURSES_LIBRARIES})
  target_compile_options(he PRIVATE -Wall -Wextra -pedantic -Werror)
endif()
