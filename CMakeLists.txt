cmake_minimum_required(VERSION 3.23)

set(CMAKE_C_COMPILER_TARGET "x86_64-linux-gnu")
set(CMAKE_CXX_COMPILER_TARGET "x86_64-linux-gnu")

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

project(Doombuntu C CXX)

include(FetchContent)
FetchContent_Declare(
    SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG release-3.4.12
    OVERRIDE_FIND_PACKAGE
)

find_package(X11 REQUIRED COMPONENTS Xext)
find_library(NSL_LIB nsl)
find_library(MATHS_LIB m)
find_package(SDL3)

add_executable(Doombuntu)

target_sources(Doombuntu
    PRIVATE
        src/linuxdoom-1.10/am_map.c
        src/linuxdoom-1.10/d_items.c
        src/linuxdoom-1.10/d_main.c
        src/linuxdoom-1.10/d_net.c
        src/linuxdoom-1.10/doomdef.c
        src/linuxdoom-1.10/doomstat.c
        src/linuxdoom-1.10/dstrings.c
        src/linuxdoom-1.10/f_finale.c
        src/linuxdoom-1.10/f_wipe.c
        src/linuxdoom-1.10/g_game.c
        src/linuxdoom-1.10/hu_lib.c
        src/linuxdoom-1.10/hu_stuff.c
        src/linuxdoom-1.10/i_main.c
        src/linuxdoom-1.10/i_net.c
        src/linuxdoom-1.10/i_sound.c
        src/linuxdoom-1.10/i_system.c
        src/linuxdoom-1.10/i_video.cpp
        src/linuxdoom-1.10/info.c
        src/linuxdoom-1.10/m_argv.c
        src/linuxdoom-1.10/m_bbox.c
        src/linuxdoom-1.10/m_cheat.c
        src/linuxdoom-1.10/m_fixed.c
        src/linuxdoom-1.10/m_menu.c
        src/linuxdoom-1.10/m_misc.c
        src/linuxdoom-1.10/m_random.c
        src/linuxdoom-1.10/m_swap.c
        src/linuxdoom-1.10/p_ceilng.c
        src/linuxdoom-1.10/p_doors.c
        src/linuxdoom-1.10/p_enemy.c
        src/linuxdoom-1.10/p_floor.c
        src/linuxdoom-1.10/p_inter.c
        src/linuxdoom-1.10/p_lights.c
        src/linuxdoom-1.10/p_map.c
        src/linuxdoom-1.10/p_maputl.c
        src/linuxdoom-1.10/p_mobj.c
        src/linuxdoom-1.10/p_plats.c
        src/linuxdoom-1.10/p_pspr.c
        src/linuxdoom-1.10/p_saveg.c
        src/linuxdoom-1.10/p_setup.c
        src/linuxdoom-1.10/p_sight.c
        src/linuxdoom-1.10/p_spec.c
        src/linuxdoom-1.10/p_switch.c
        src/linuxdoom-1.10/p_telept.c
        src/linuxdoom-1.10/p_tick.c
        src/linuxdoom-1.10/p_user.c
        src/linuxdoom-1.10/r_bsp.c
        src/linuxdoom-1.10/r_data.c
        src/linuxdoom-1.10/r_draw.c
        src/linuxdoom-1.10/r_main.c
        src/linuxdoom-1.10/r_plane.c
        src/linuxdoom-1.10/r_segs.c
        src/linuxdoom-1.10/r_sky.c
        src/linuxdoom-1.10/r_things.c
        src/linuxdoom-1.10/s_sound.c
        src/linuxdoom-1.10/sounds.c
        src/linuxdoom-1.10/st_lib.c
        src/linuxdoom-1.10/st_stuff.c
        src/linuxdoom-1.10/tables.c
        src/linuxdoom-1.10/v_video.c
        src/linuxdoom-1.10/w_wad.c
        src/linuxdoom-1.10/wi_stuff.c
        src/linuxdoom-1.10/z_zone.c
)

target_compile_definitions(Doombuntu
    PRIVATE
        NORMALUNIX
        LINUX
        USE_SDL
)

target_link_libraries(Doombuntu
    PRIVATE
    X11::X11
    X11::Xext
    SDL3::SDL3
    ${NSL_LIB}
    ${MATHS_LIB}
)
