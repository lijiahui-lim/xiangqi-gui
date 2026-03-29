cmake_minimum_required(VERSION 3.16)
project(XiangqiGUI LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED sdl2)
pkg_check_modules(SDL2_TTF REQUIRED SDL2_ttf)
pkg_check_modules(SDL2_GFX REQUIRED SDL2_gfx)
find_package(CURL REQUIRED)

add_executable(xiangqi
    src/main.cpp
    src/board.cpp
    src/engine.cpp
    src/cloudbook.cpp
    src/renderer_core.cpp
    src/renderer_panel.cpp
    src/renderer_edit.cpp
    src/gui_core.cpp
    src/gui_engine.cpp
    src/gui_game.cpp
    src/gui_ui.cpp
    src/gui_filebrowser.cpp
    src/gui_pgn.cpp
    src/rules_core.cpp
)

target_include_directories(xiangqi PRIVATE
    ${SDL2_INCLUDE_DIRS}
    ${SDL2_TTF_INCLUDE_DIRS}
    ${SDL2_GFX_INCLUDE_DIRS}
    ${CURL_INCLUDE_DIRS}
    src
)

target_link_libraries(xiangqi PRIVATE
    ${SDL2_LIBRARIES}
    ${SDL2_TTF_LIBRARIES}
    ${SDL2_GFX_LIBRARIES}
    CURL::libcurl
    pthread
)

target_compile_options(xiangqi PRIVATE
    ${SDL2_CFLAGS_OTHER}
)
