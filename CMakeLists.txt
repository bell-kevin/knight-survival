# ==============================================================================
# CMakeLists.txt - Knight Survival
# ==============================================================================
# Native build (needs SDL2, e.g. `sudo apt install libsdl2-dev`):
#   cmake -B build
#   cmake --build build
#   ./build/knight-server                  # optional: LAN server on port 9000
#   ./build/knight-survival [server-ip]    # plays solo if no server answers
#
# Web build (needs Emscripten: https://emscripten.org), what GitHub Pages serves:
#   emcmake cmake -B build-web -DCMAKE_BUILD_TYPE=Release
#   cmake --build build-web
#   python3 -m http.server -d build-web/site 8080   # then open http://localhost:8080
# ==============================================================================
cmake_minimum_required(VERSION 3.16)
project(KnightSurvival VERSION 2.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True) # Enforce the specified C++ standard
set(CMAKE_CXX_EXTENSIONS OFF)         # Disable compiler-specific extensions

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug) # Default to Debug if not specified
endif()

set(WARNINGS -Wall -Wextra)

# The levels: every Tiled map in maps/ (and the tilesets they use) is compiled into the game, so
# the server, the native game and the web game all play the same levels. Editing or adding a map
# makes the next build pick it up.
file(GLOB LEVEL_FILES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/maps/*.tmx ${CMAKE_SOURCE_DIR}/maps/*.tsx)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${LEVEL_FILES})
set(LEVELS_SOURCE "// Made by CMake from the files in maps/: edit those, not this.\n#include \"level.h\"\n\n")
string(APPEND LEVELS_SOURCE "const std::vector<MapFile> &builtInMapFiles()\n{\n    static const std::vector<MapFile> files = {\n")
foreach(LEVEL_FILE ${LEVEL_FILES})
    get_filename_component(LEVEL_NAME ${LEVEL_FILE} NAME)
    file(READ ${LEVEL_FILE} LEVEL_TEXT)
    string(APPEND LEVELS_SOURCE "        {\"${LEVEL_NAME}\", R\"knightmap(${LEVEL_TEXT})knightmap\"},\n")
endforeach()
string(APPEND LEVELS_SOURCE "    };\n    return files;\n}\n")
file(WRITE ${CMAKE_BINARY_DIR}/levels.cpp.new "${LEVELS_SOURCE}")
configure_file(${CMAKE_BINARY_DIR}/levels.cpp.new ${CMAKE_BINARY_DIR}/levels.cpp COPYONLY) # only touched when it changes

# The game rules, shared by the server, the native game and the web game
add_library(world STATIC src/world.cpp src/level.cpp ${CMAKE_BINARY_DIR}/levels.cpp)
target_include_directories(world PUBLIC src)
target_compile_options(world PRIVATE ${WARNINGS})

if(EMSCRIPTEN)
    # ---------------------------------------------------------------- web game
    set(SITE_DIR ${CMAKE_BINARY_DIR}/site)
    set(WEB_DIR ${CMAKE_SOURCE_DIR}/web)
    set(ASSETS ${CMAKE_SOURCE_DIR}/assets)

    add_executable(knight-survival src/main.cpp src/session_web.cpp)
    target_link_libraries(knight-survival PRIVATE world)
    target_compile_options(knight-survival PRIVATE ${WARNINGS} -sUSE_SDL=2)
    set_target_properties(knight-survival PROPERTIES
        OUTPUT_NAME index
        SUFFIX .html
        RUNTIME_OUTPUT_DIRECTORY ${SITE_DIR}
        LINK_DEPENDS ${WEB_DIR}/shell.html)
    target_link_options(knight-survival PRIVATE
        -sUSE_SDL=2
        -sALLOW_MEMORY_GROWTH=1
        -sENVIRONMENT=web
        -sINVOKE_RUN=0                                  # the page starts the game when you press Play
        -sEXPORTED_RUNTIME_METHODS=callMain,ccall
        "SHELL:--shell-file \"${WEB_DIR}/shell.html\""
        # Pack the sprites and tiles into the game (only the .bmp files)
        "SHELL:--embed-file \"${ASSETS}@/assets\""
        "SHELL:--exclude-file *.h")
    # Rebuild when a picture changes
    file(GLOB_RECURSE ASSET_FILES CONFIGURE_DEPENDS ${ASSETS}/*.bmp)
    set_property(TARGET knight-survival APPEND PROPERTY LINK_DEPENDS ${ASSET_FILES})

    # Everything else the page needs goes next to it
    add_custom_command(TARGET knight-survival POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy ${WEB_DIR}/net.js ${WEB_DIR}/favicon.png ${WEB_DIR}/preview.png ${SITE_DIR}
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${WEB_DIR}/vendor ${SITE_DIR}/vendor
        COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/LICENSE ${SITE_DIR}/LICENSE.txt
        VERBATIM)
else()
    # ---------------------------------------------------------------- native game
    find_package(SDL2 CONFIG QUIET)
    if(TARGET SDL2::SDL2)
        set(SDL2_TARGET SDL2::SDL2)
    else()
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)
        set(SDL2_TARGET PkgConfig::SDL2)
    endif()

    add_executable(knight-survival src/main.cpp src/session_native.cpp)
    target_link_libraries(knight-survival PRIVATE world ${SDL2_TARGET})
    target_compile_definitions(knight-survival PRIVATE ASSET_DIR="${CMAKE_SOURCE_DIR}/assets")
    target_compile_options(knight-survival PRIVATE ${WARNINGS})

    # LAN game server (no SDL needed)
    add_executable(knight-server server/server.cpp)
    target_link_libraries(knight-server PRIVATE world)
    target_compile_options(knight-server PRIVATE ${WARNINGS})

    # Terminal client for poking at the server by hand
    add_executable(chat-client client/client.cpp)
    target_compile_options(chat-client PRIVATE ${WARNINGS})

    # Tests for the game rules: ctest --test-dir build
    enable_testing()
    add_executable(world-test tests/world_test.cpp)
    target_link_libraries(world-test PRIVATE world)
    target_compile_options(world-test PRIVATE ${WARNINGS})
    add_test(NAME world COMMAND world-test)
endif()
