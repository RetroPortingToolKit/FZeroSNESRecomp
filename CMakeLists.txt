cmake_minimum_required(VERSION 3.20)
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" FZERO_VERSION LIMIT_COUNT 1)
project(FZeroSNESRecomp VERSION ${FZERO_VERSION} LANGUAGES C CXX)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)
find_package(LibXml2 REQUIRED)
find_package(PNG REQUIRED)
find_library(GIF_LIBRARY NAMES gif REQUIRED)
find_path(RAPIDJSON_INCLUDE_DIR rapidjson/document.h REQUIRED)

include(CTest)
set(SNESRECOMP_ROOT "${CMAKE_SOURCE_DIR}/snesrecomp" CACHE PATH "Framework checkout")
add_library(fzero_actor STATIC src/fzero_actor.c src/fzero_actor_progress.c src/fzero_actor_contact.c src/fzero_actor_tick.c src/fzero_versus.c src/fzero_sound.c)
target_include_directories(fzero_actor PUBLIC "${CMAKE_SOURCE_DIR}/src")
add_library(fzero_camera STATIC src/fzero_camera.c src/fzero_ship.c src/fzero_native_ui.c)
target_link_libraries(fzero_camera PUBLIC fzero_actor)
target_include_directories(fzero_camera PUBLIC "${CMAKE_SOURCE_DIR}/src")
if(NOT MSVC)
    target_compile_options(fzero_actor PRIVATE -Wall -Wextra -Werror)
    target_compile_options(fzero_camera PRIVATE -Wall -Wextra -Werror)
endif()
add_library(fzero_tracks STATIC src/fzero_tracks.c src/fzero_menu_music.c src/fzero_course.c src/fzero_course_file.c src/fzero_title.c src/fzero_music_sources.c src/fzero_packs.cpp src/fzero_fzedit.cpp src/fzero_content_import.cpp
    "${SNESRECOMP_ROOT}/runner/src/content_pack.c"
    "${SNESRECOMP_ROOT}/runner/src/content_patch.c"
    "${SNESRECOMP_ROOT}/runner/src/sha256.c")
target_include_directories(fzero_tracks PUBLIC "${CMAKE_SOURCE_DIR}/src" "${SNESRECOMP_ROOT}/runner/src")
target_include_directories(fzero_tracks SYSTEM PRIVATE "${RAPIDJSON_INCLUDE_DIR}")
include("${SNESRECOMP_ROOT}/runner/data_packs.cmake")
snesrecomp_target_data_packs(fzero_tracks)
target_link_libraries(fzero_tracks PUBLIC LibXml2::LibXml2 PNG::PNG ${GIF_LIBRARY})
add_executable(FZeroInspectCourses tools/inspect_courses.c)
target_link_libraries(FZeroInspectCourses PRIVATE fzero_tracks)
add_executable(FZeroExportCourses tools/export_courses.c)
target_link_libraries(FZeroExportCourses PRIVATE fzero_tracks)
add_executable(FZeroInspectPacks tools/inspect_packs.c)
target_link_libraries(FZeroInspectPacks PRIVATE fzero_tracks)
add_executable(FZeroImportContent tools/import_content.cpp)
target_link_libraries(FZeroImportContent PRIVATE fzero_tracks)
if(NOT MSVC)
    target_compile_options(fzero_tracks PRIVATE -Wall -Wextra -Werror)
endif()
option(FZERO_BUILD_GAME "Build game hosts (requires privately generated ROM sources)" ON)
add_library(fzero_video STATIC src/fzero_gameplay_settings.c src/fzero_video.c src/fzero_mode7.c src/fzero_replay.c src/fzero_hdma.c)
target_include_directories(fzero_video PUBLIC "${CMAKE_SOURCE_DIR}/src" "${SNESRECOMP_ROOT}/runner/src")
target_link_libraries(fzero_video PUBLIC fzero_camera)
if(NOT MSVC)
    target_compile_options(fzero_video PRIVATE -Wall -Wextra -Werror)
    target_link_libraries(fzero_video PUBLIC m)
endif()
if(BUILD_TESTING)
    # A private stock ROM is an explicit argument, never a distributed fixture.
    # Execute each scalar adapter's reference routine on the callback-bus CPU;
    # this target has no SDL/audio output and never advances a game frame.
    add_executable(fzero_actor_oracle tests/test_actor_oracle.c
        "${SNESRECOMP_ROOT}/runner/src/snes/interp816.c"
        "${SNESRECOMP_ROOT}/runner/src/sha256.c")
    target_include_directories(fzero_actor_oracle PRIVATE "${SNESRECOMP_ROOT}/runner/src")
    target_link_libraries(fzero_actor_oracle PRIVATE fzero_actor fzero_camera)
    set(FZERO_TEST_ROM "" CACHE FILEPATH "Private verified stock ROM for actor parity tests")
    if(FZERO_TEST_ROM)
        add_test(NAME fzero_actor_stock_parity COMMAND fzero_actor_oracle "${FZERO_TEST_ROM}")
    endif()
    if(NOT MSVC)
        target_compile_options(fzero_actor_oracle PRIVATE -Wall -Wextra -Werror)
    endif()
    find_package(Python3 QUIET COMPONENTS Interpreter)
    if(Python3_Interpreter_FOUND)
        add_test(NAME fzero_content_import COMMAND "${Python3_EXECUTABLE}" -X utf8
            "${CMAKE_SOURCE_DIR}/tests/test_content_import.py"
            --tool $<TARGET_FILE:FZeroImportContent>)
    endif()
    add_executable(fzero_tracks_tests tests/test_tracks.c)
    target_link_libraries(fzero_tracks_tests PRIVATE fzero_tracks)
    add_test(NAME fzero_tracks COMMAND fzero_tracks_tests "${CMAKE_SOURCE_DIR}/assets/track-packs")
    add_executable(fzero_music_sources_tests tests/test_music_sources.c)
    target_link_libraries(fzero_music_sources_tests PRIVATE fzero_tracks)
    add_test(NAME fzero_music_sources COMMAND fzero_music_sources_tests)
    add_executable(fzero_course_parser_tests tests/test_course_parser.c)
    target_link_libraries(fzero_course_parser_tests PRIVATE fzero_tracks)
    add_test(NAME fzero_course_parser COMMAND fzero_course_parser_tests)
    add_executable(fzero_course_saves_tests tests/test_course_saves.c src/fzero_course_saves.c)
    target_link_libraries(fzero_course_saves_tests PRIVATE fzero_tracks)
    target_include_directories(fzero_course_saves_tests PRIVATE "${SNESRECOMP_ROOT}/recompiler/runtime")
    add_test(NAME fzero_course_saves COMMAND fzero_course_saves_tests)
    add_executable(fzero_hdma_tests tests/test_hdma.c)
    target_link_libraries(fzero_hdma_tests PRIVATE fzero_video)
    add_test(NAME fzero_hdma COMMAND fzero_hdma_tests)
    add_executable(fzero_video_tests tests/test_video.c)
    target_link_libraries(fzero_video_tests PRIVATE fzero_video)
    add_test(NAME fzero_video COMMAND fzero_video_tests)
    add_executable(fzero_mode7_tests tests/test_mode7.c)
    target_link_libraries(fzero_mode7_tests PRIVATE fzero_video)
    add_test(NAME fzero_mode7 COMMAND fzero_mode7_tests)
    # Neither of these needs SDL, a ROM, or the generated sources: they are
    # the host-side logic behind the save-state menu and rewind bindings, so
    # they build and run in the ROM-free configuration too.
    add_executable(fzero_hotkeys_tests tests/test_hotkeys.c src/fzero_hotkeys.c)
    target_include_directories(fzero_hotkeys_tests PRIVATE "${CMAKE_SOURCE_DIR}/src")
    add_test(NAME fzero_hotkeys COMMAND fzero_hotkeys_tests)
    add_executable(fzero_state_mode_tests tests/test_state_mode.c src/fzero_state_mode.c)
    target_include_directories(fzero_state_mode_tests PRIVATE "${CMAKE_SOURCE_DIR}/src")
    add_test(NAME fzero_state_mode COMMAND fzero_state_mode_tests)
    if(NOT MSVC)
        target_compile_options(fzero_hotkeys_tests PRIVATE -Wall -Wextra -Werror)
        target_compile_options(fzero_state_mode_tests PRIVATE -Wall -Wextra -Werror)
    endif()
endif()
if(NOT FZERO_BUILD_GAME)
    return()
endif()

# Both dependencies are pinned submodules of this repository (see
# .gitmodules); `bash tools/bootstrap.sh` initializes them. Point
# SNESRECOMP_ROOT at a worktree instead when developing framework changes
# alongside the game.
set(SNESRECOMP_ROOT "${CMAKE_SOURCE_DIR}/snesrecomp" CACHE PATH
    "Path to the snesrecomp checkout used by this game")
set(RECOMP_UI_ROOT "${CMAKE_SOURCE_DIR}/recomp-ui" CACHE PATH
    "Path to the shared recomp-ui launcher checkout")
set(SNESRECOMP_SDL_BACKEND "SDL3" CACHE STRING
    "Desktop SDL backend (SDL3 or SDL2)")
set(RECOMP_UI_ENABLE_MODS ON CACHE BOOL "F-Zero built-in native presentation mod" FORCE)
include(${SNESRECOMP_ROOT}/runner/runner.cmake)
include(${SNESRECOMP_ROOT}/runner/render_workers.cmake)
include(${RECOMP_UI_ROOT}/recomp_ui.cmake)
find_package(OpenGL REQUIRED)

add_executable(FZeroRenderCapture tools/render_capture.c src/fzero_renderer.c)
target_include_directories(FZeroRenderCapture PRIVATE ${SNESRECOMP_RUNNER_INCLUDE_DIRS})
target_link_libraries(FZeroRenderCapture PRIVATE fzero_video)
snesrecomp_target_render_workers(FZeroRenderCapture)
add_executable(FZeroRendererBenchmark tools/benchmark_renderer.c src/fzero_renderer.c)
target_include_directories(FZeroRendererBenchmark PRIVATE ${SNESRECOMP_RUNNER_INCLUDE_DIRS})
target_link_libraries(FZeroRendererBenchmark PRIVATE fzero_video)
snesrecomp_target_render_workers(FZeroRendererBenchmark)
if(BUILD_TESTING)
    add_executable(fzero_renderer_tests tests/test_renderer.c src/fzero_renderer.c)
    add_executable(fzero_content_ui_tests tests/test_content_ui.cpp src/fzero_content_ui.cpp)
    set_property(TARGET fzero_content_ui_tests PROPERTY CXX_STANDARD 17)
    target_include_directories(fzero_content_ui_tests PRIVATE "${CMAKE_SOURCE_DIR}/src" "${RECOMP_UI_ROOT}/src" "${RAPIDJSON_INCLUDE_DIR}")
    snesrecomp_target_data_packs(fzero_content_ui_tests)
    add_test(NAME fzero_content_ui COMMAND fzero_content_ui_tests)
    add_executable(fzero_mods_tests tests/test_mods.c src/fzero_mods.c src/fzero_tracks_mods.c)
    target_include_directories(fzero_mods_tests PRIVATE "${RECOMP_UI_ROOT}/src")
    target_link_libraries(fzero_mods_tests PRIVATE fzero_video fzero_tracks)
    if(NOT MSVC)
        target_compile_options(fzero_mods_tests PRIVATE -Wall -Wextra -Werror)
    endif()
    add_test(NAME fzero_mods COMMAND fzero_mods_tests)
    target_include_directories(fzero_renderer_tests PRIVATE ${SNESRECOMP_RUNNER_INCLUDE_DIRS})
    target_link_libraries(fzero_renderer_tests PRIVATE fzero_video)
    snesrecomp_target_render_workers(fzero_renderer_tests)
    if(NOT MSVC)
        target_compile_options(fzero_renderer_tests PRIVATE -Wall -Wextra -Werror)
    endif()
    add_test(NAME fzero_renderer COMMAND fzero_renderer_tests)
endif()

# Release version stamp defaults to VERSION; override explicitly for local builds.
# NOTE: quote it when configuring - PowerShell rewrites an unquoted
# -DSNESRECOMP_BUILD_VERSION=0.1.0 into "0".
set(SNESRECOMP_BUILD_VERSION "${PROJECT_VERSION}" CACHE STRING
    "Version stamp baked into crash reports")

set(FZERO_GEN_DIR "${CMAKE_SOURCE_DIR}/src/gen" CACHE PATH
    "Directory containing the private generated F-Zero C sources")
file(GLOB FZERO_GEN_SOURCES CONFIGURE_DEPENDS "${FZERO_GEN_DIR}/*.c")
if(NOT FZERO_GEN_SOURCES)
    message(FATAL_ERROR
        "${FZERO_GEN_DIR} is empty - stage a verified fzero.sfc and run "
        "`bash tools/regen.sh` before building.")
endif()

# The headless host links no SDL at all, so anything in the runner that
# includes desktop/sdl_compat.h has to be held back from the shared source
# list and handed to the desktop host explicitly. keybinds.c and
# host_report.c were always in that set; snes_savestate_menu.c and
# snes_osd.c joined the runner later and pull SDL in for their keycodes and
# their tick clock.
list(REMOVE_ITEM SNESRECOMP_RUNNER_SOURCES
    "${SNESRECOMP_ROOT}/runner/src/keybinds.c"
    "${SNESRECOMP_ROOT}/runner/src/host_report.c"
    "${SNESRECOMP_ROOT}/runner/src/snes_savestate_menu.c"
    "${SNESRECOMP_ROOT}/runner/src/snes_osd.c")

set(FZERO_DELUXE_GEN_DIR "" CACHE PATH "Private generated BS Deluxe native sources")
set(FZERO_DELUXE_DATA_FILE "" CACHE FILEPATH "Imported BS Deluxe payload to embed")
if(FZERO_DELUXE_GEN_DIR)
    # The payload ships inside the executable: a download can never be missing
    # it and the game can never fail to launch over it. Default to the file the
    # importer writes beside the generated sources.
    if(NOT FZERO_DELUXE_DATA_FILE)
        get_filename_component(FZERO_DELUXE_WORK "${FZERO_DELUXE_GEN_DIR}" DIRECTORY)
        set(FZERO_DELUXE_DATA_FILE "${FZERO_DELUXE_WORK}/mods/bs-deluxe.dat")
    endif()
    if(NOT EXISTS "${FZERO_DELUXE_DATA_FILE}")
        message(FATAL_ERROR "BS Deluxe payload not found at ${FZERO_DELUXE_DATA_FILE}. "
                            "Run tools/regen_bs_deluxe.py, or set FZERO_DELUXE_DATA_FILE.")
    endif()
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    set(FZERO_DELUXE_PAYLOAD_SOURCE "${CMAKE_BINARY_DIR}/generated/bs_deluxe_payload.c")
    add_custom_command(OUTPUT "${FZERO_DELUXE_PAYLOAD_SOURCE}"
        COMMAND ${Python3_EXECUTABLE} "${CMAKE_SOURCE_DIR}/tools/embed_payload.py"
                "${FZERO_DELUXE_DATA_FILE}" "${FZERO_DELUXE_PAYLOAD_SOURCE}"
        DEPENDS "${FZERO_DELUXE_DATA_FILE}" "${CMAKE_SOURCE_DIR}/tools/embed_payload.py"
        COMMENT "Embedding BS Deluxe payload" VERBATIM)
    add_custom_target(fzero_deluxe_payload_source DEPENDS "${FZERO_DELUXE_PAYLOAD_SOURCE}")
    file(GLOB DELUXE_SOURCES CONFIGURE_DEPENDS "${FZERO_DELUXE_GEN_DIR}/*.c")
    if(NOT DELUXE_SOURCES OR NOT EXISTS "${FZERO_DELUXE_GEN_DIR}/module_namespace.h")
        message(FATAL_ERROR "Generate and import BS Deluxe before enabling its native module")
    endif()
    add_library(fzero_deluxe_native OBJECT ${DELUXE_SOURCES})
    target_include_directories(fzero_deluxe_native PRIVATE
        "${CMAKE_SOURCE_DIR}/recomp" ${SNESRECOMP_RUNNER_INCLUDE_DIRS})
    if(NOT MSVC)
        target_compile_options(fzero_deluxe_native PRIVATE -w)
    endif()
endif()

# The headless host keeps its own host_report stubs: it links no SDL, and
# host_report.c includes desktop/sdl_compat.h.
add_executable(FZeroSNESRecompHeadless
    tests/probe_cgp_rules.c
    tests/fzero_actor_live.c
    ${SNESRECOMP_RUNNER_SOURCES}
    ${FZERO_GEN_SOURCES}
    src/headless_host.c
    src/headless_main.c
    src/fzero_runtime.c)
target_link_libraries(FZeroSNESRecompHeadless PRIVATE fzero_actor)

# The desktop host takes the REAL host_report.c (always-on breadcrumb ring,
# crash minidump writer, build-version stamp) and the REAL keybinds.c, so the
# launcher's Controls page and the game read the same keybinds.ini.
add_executable(FZeroSNESRecomp
    ${SNESRECOMP_RUNNER_SOURCES}
    "${SNESRECOMP_ROOT}/runner/src/host_report.c"
    "${SNESRECOMP_ROOT}/runner/src/keybinds.c"
    "${SNESRECOMP_ROOT}/runner/src/snes_savestate_menu.c"
    "${SNESRECOMP_ROOT}/runner/src/snes_osd.c"
    "${RECOMP_UI_ROOT}/src/third_party/gl_core_3_1.c"
    ${FZERO_GEN_SOURCES}
    src/sdl_main.c
    src/fzero_content_ui.cpp
    src/fzero_diagnostics.c
    src/fzero_fault.c
    src/fzero_mods.c
    src/fzero_tracks_mods.c
    src/fzero_hotkeys.c
    src/fzero_gamepad.c
    src/fzero_local_controls.c
    src/fzero_runtime.c)

# Defaults scoped to this game target: the shared stores and launcher retain
# C/V and F8 for other games. F-Zero does not use the generic R renderer hotkey.
target_compile_definitions(FZeroSNESRecomp PRIVATE
    RECOMP_DEFAULT_KEY_L=SDL_SCANCODE_D
    RECOMP_DEFAULT_KEY_R=SDL_SCANCODE_C
    RECOMP_UI_DEFAULT_REWIND_KEY="R"
    RECOMP_UI_DEFAULT_RENDERER_KEY="")

function(configure_fzero_target target)
    target_sources(${target} PRIVATE src/fzero_capture.c src/fzero_capture_records.cpp)
    target_include_directories(${target} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
    target_compile_definitions(${target} PRIVATE SNESRECOMP_BUILD_VERSION="${SNESRECOMP_BUILD_VERSION}")
    target_sources(${target} PRIVATE src/fzero_deluxe.c src/fzero_gameplay.c src/fzero_vehicles.c src/fzero_state_mode.c src/fzero_msu.c src/fzero_track_runtime.c src/fzero_course_hooks.c src/fzero_course_menu.c src/fzero_course_saves.c src/fzero_records.c)
    target_link_libraries(${target} PRIVATE fzero_tracks)
    if(FZERO_DELUXE_GEN_DIR)
        target_sources(${target} PRIVATE $<TARGET_OBJECTS:fzero_deluxe_native>
                                         "${FZERO_DELUXE_PAYLOAD_SOURCE}")
        add_dependencies(${target} fzero_deluxe_payload_source)
        target_compile_definitions(${target} PRIVATE FZERO_HAS_DELUXE=1)
    endif()
    target_sources(${target} PRIVATE src/fzero_renderer.c src/fzero_versus_video.c src/fzero_local.c)
    snesrecomp_target_render_workers(${target})
    target_link_libraries(${target} PRIVATE fzero_video)
    target_include_directories(${target} PRIVATE
        "${CMAKE_SOURCE_DIR}"
        "${CMAKE_SOURCE_DIR}/src"
        "${CMAKE_SOURCE_DIR}/recomp"
        ${SNESRECOMP_RUNNER_INCLUDE_DIRS})
    target_compile_definitions(${target} PRIVATE
        SYSTEM_VOLUME_MIXER_AVAILABLE=0
        SNESRECOMP_TRACE=$<BOOL:${SNESRECOMP_ENABLE_TRACE}>
        SNESRECOMP_REVERSE_DEBUG=0)
    if(NOT MSVC)
        # The recompiled C is machine-generated; quiet its warnings and
        # tolerate the K&R-style implicit declarations clang rejects.
        target_compile_options(${target} PRIVATE
            $<$<COMPILE_LANGUAGE:C>:-w;-Wno-implicit-function-declaration>)
        target_link_libraries(${target} PRIVATE m)
    endif()
    target_link_libraries(${target} PRIVATE ${SNESRECOMP_RUNNER_LIBRARIES})
endfunction()

configure_fzero_target(FZeroSNESRecompHeadless)
configure_fzero_target(FZeroSNESRecomp)
snesrecomp_target_post_mortem(FZeroSNESRecomp)
set(FZERO_DESKTOP_NAME "FZeroSNESRecomp" CACHE STRING "Desktop executable name for isolated experiments")
set_target_properties(FZeroSNESRecomp PROPERTIES OUTPUT_NAME "${FZERO_DESKTOP_NAME}")
# Hosts include "desktop/sdl_compat.h" rather than <SDL.h> so both majors build.
snesrecomp_target_glsl_shader(FZeroSNESRecomp)
snesrecomp_target_sdl(FZeroSNESRecomp)

if(BUILD_TESTING AND SNESRECOMP_SDL_BACKEND STREQUAL "SDL3")
    add_executable(fzero_gamepad_tests tests/test_gamepad.c src/fzero_gamepad.c src/fzero_hotkeys.c)
    target_include_directories(fzero_gamepad_tests PRIVATE "${CMAKE_SOURCE_DIR}/src" "${RECOMP_UI_ROOT}/src" ${SNESRECOMP_RUNNER_INCLUDE_DIRS})
    snesrecomp_target_sdl(fzero_gamepad_tests)
    add_test(NAME fzero_gamepad COMMAND fzero_gamepad_tests)
    add_executable(fzero_local_controls_tests tests/test_local_controls.c src/fzero_local_controls.c src/fzero_gamepad.c src/fzero_hotkeys.c "${SNESRECOMP_ROOT}/runner/src/snes_overlay_draw.c")
    target_include_directories(fzero_local_controls_tests PRIVATE "${CMAKE_SOURCE_DIR}/src" "${RECOMP_UI_ROOT}/src" ${SNESRECOMP_RUNNER_INCLUDE_DIRS})
    target_link_libraries(fzero_local_controls_tests PRIVATE fzero_camera)
    snesrecomp_target_sdl(fzero_local_controls_tests)
    add_test(NAME fzero_local_controls COMMAND fzero_local_controls_tests)
endif()
target_link_libraries(FZeroSNESRecomp PRIVATE OpenGL::GL)
recomp_target_launcher_ui(FZeroSNESRecomp CONSOLE snes)
# Identify dependencies as well as the game in player performance reports.
find_package(Git QUIET)
function(fzero_revision directory output)
    set(revision "unknown")
    if(GIT_FOUND)
        execute_process(COMMAND "${GIT_EXECUTABLE}" describe --always --dirty --abbrev=12
            WORKING_DIRECTORY "${directory}" OUTPUT_VARIABLE revision
            RESULT_VARIABLE revision_result OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
        if(NOT revision_result EQUAL 0)
            set(revision "unknown")
        endif()
    endif()
    set(${output} "${revision}" PARENT_SCOPE)
endfunction()
fzero_revision("${CMAKE_CURRENT_SOURCE_DIR}" FZERO_SOURCE_REVISION)
fzero_revision("${SNESRECOMP_ROOT}" FZERO_FRAMEWORK_REVISION)
fzero_revision("${RECOMP_UI_ROOT}" FZERO_UI_REVISION)
# Dirty builds need more than an unchanged Git commit stamp for safe replay.
file(GLOB FZERO_BUILD_INPUTS CONFIGURE_DEPENDS src/*.c src/*.cpp src/*.h)
list(APPEND FZERO_BUILD_INPUTS "${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${FZERO_BUILD_INPUTS})
set(fingerprint "${SNESRECOMP_BUILD_VERSION};${FZERO_SOURCE_REVISION};${FZERO_FRAMEWORK_REVISION};${FZERO_UI_REVISION};${CMAKE_BUILD_TYPE};${CMAKE_C_COMPILER_ID};${CMAKE_C_COMPILER_VERSION};${CMAKE_C_FLAGS};${CMAKE_C_FLAGS_RELEASE}")
foreach(input IN LISTS FZERO_BUILD_INPUTS)
    file(SHA256 "${input}" input_hash)
    get_filename_component(input_name "${input}" NAME)
    string(APPEND fingerprint ";${input_name}:${input_hash}")
endforeach()
string(SHA256 FZERO_REPLAY_BUILD_ID "${fingerprint}")
configure_file(src/fzero_build.h.in generated/fzero_build.h @ONLY)
target_include_directories(FZeroSNESRecomp PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
add_custom_command(TARGET FZeroSNESRecomp POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/img/boxart.tga"
        "$<TARGET_FILE_DIR:FZeroSNESRecomp>/assets/img/boxart.tga"
    VERBATIM)
add_custom_command(TARGET FZeroSNESRecomp POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders"
        "$<TARGET_FILE_DIR:FZeroSNESRecomp>/assets/shaders"
    COMMENT "Staging F-Zero shader presets"
    VERBATIM)

if(SNESRECOMP_BUILD_VERSION)
    target_compile_definitions(FZeroSNESRecomp PRIVATE
        SNESRECOMP_BUILD_VERSION="${SNESRECOMP_BUILD_VERSION}")
endif()

if(BUILD_TESTING AND FZERO_DELUXE_GEN_DIR)
    # Locks the contract that matters for a download: the payload is in the
    # binary, a file beside it is only an override, and nothing here can
    # stop the game starting.
    add_executable(fzero_deluxe_tests tests/test_deluxe.c tests/deluxe_stubs.c
        src/fzero_deluxe.c "${SNESRECOMP_ROOT}/runner/src/sha256.c"
        "${FZERO_DELUXE_PAYLOAD_SOURCE}")
    add_dependencies(fzero_deluxe_tests fzero_deluxe_payload_source)
    target_compile_definitions(fzero_deluxe_tests PRIVATE FZERO_HAS_DELUXE=1)
    target_include_directories(fzero_deluxe_tests PRIVATE
        "${CMAKE_SOURCE_DIR}/src" ${SNESRECOMP_RUNNER_INCLUDE_DIRS})
    if(NOT MSVC)
        target_compile_options(fzero_deluxe_tests PRIVATE -Wall -Wextra -Werror)
    endif()
    add_test(NAME fzero_deluxe COMMAND fzero_deluxe_tests
             WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
endif()

# Shipped descriptors, patches and credits live separately from user inputs/settings.
set(FZERO_PACK_SOURCE "" CACHE PATH "Locally extracted, validated standalone release packs")
option(FZERO_STAGE_RELEASE_CONTENT "Stage optional release packs and music after desktop links" ON)
if(FZERO_PACK_SOURCE AND FZERO_STAGE_RELEASE_CONTENT)
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    file(GLOB_RECURSE FZERO_PACK_PAYLOAD CONFIGURE_DEPENDS "${FZERO_PACK_SOURCE}/*")
    set_property(TARGET FZeroSNESRecomp APPEND PROPERTY LINK_DEPENDS ${FZERO_PACK_PAYLOAD}
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/bundled_content.py")
    add_custom_command(TARGET FZeroSNESRecomp POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${FZERO_PACK_SOURCE}" "$<TARGET_FILE_DIR:FZeroSNESRecomp>/mods/packs"
        COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/tools/bundled_content.py"
            --source "${FZERO_PACK_SOURCE}" --mods "$<TARGET_FILE_DIR:FZeroSNESRecomp>/mods"
        VERBATIM)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/music/cgp/cgp.msu")
        find_package(Python3 COMPONENTS Interpreter REQUIRED)
        set_property(TARGET FZeroSNESRecomp APPEND PROPERTY LINK_DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/assets/music/cgp.json"
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/import_cgp_music.py")
        add_custom_command(TARGET FZeroSNESRecomp POST_BUILD
            COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/tools/import_cgp_music.py"
                --stage-course-from "${CMAKE_CURRENT_SOURCE_DIR}/music/cgp"
                --out "$<TARGET_FILE_DIR:FZeroSNESRecomp>/mods/packs/cgp"
            COMMENT "Staging verified CGP music by course filename" VERBATIM)
    endif()
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/music/astra-front/moon.pcm")
        find_package(Python3 COMPONENTS Interpreter REQUIRED)
        set_property(TARGET FZeroSNESRecomp APPEND PROPERTY LINK_DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/assets/music/astra-front.json"
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/import_astra_music.py")
        add_custom_command(TARGET FZeroSNESRecomp POST_BUILD
            COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/tools/import_astra_music.py"
                --stage-course-from "${CMAKE_CURRENT_SOURCE_DIR}/music/astra-front"
                --out "$<TARGET_FILE_DIR:FZeroSNESRecomp>/mods/packs/astra-front"
            COMMENT "Staging verified Astra music by course filename" VERBATIM)
    endif()
endif()
file(GLOB FZERO_COURSE_REGISTRY CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/assets/track-packs/*.ini"
    "${CMAKE_SOURCE_DIR}/assets/track-packs/*.layout"
    "${CMAKE_SOURCE_DIR}/assets/track-packs/*.ips"
    "${CMAKE_SOURCE_DIR}/assets/track-packs/*.bps"
    "${CMAKE_SOURCE_DIR}/assets/track-packs/*.hidden"
    "${CMAKE_SOURCE_DIR}/assets/track-packs/*.disabled"
    "${CMAKE_SOURCE_DIR}/assets/track-packs/*.txt")
file(READ "${CMAKE_SOURCE_DIR}/CONVERSION.md" FZERO_CONVERSION_GUIDE)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/CONVERSION.md")
string(REPLACE "(mods/PARSE_MANIFEST.md)" "(PARSE_MANIFEST.md)" FZERO_CONVERSION_GUIDE "${FZERO_CONVERSION_GUIDE}")
string(REPLACE "(docs/" "(../docs/" FZERO_CONVERSION_GUIDE "${FZERO_CONVERSION_GUIDE}")
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/generated")
file(WRITE "${CMAKE_BINARY_DIR}/generated/CONVERSION.md" "${FZERO_CONVERSION_GUIDE}")
file(GLOB FZERO_VEHICLE_REGISTRY CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/assets/vehicle-packs/*.ini"
    "${CMAKE_SOURCE_DIR}/assets/vehicle-packs/*.ips"
    "${CMAKE_SOURCE_DIR}/assets/vehicle-packs/*.md")
file(GLOB FZERO_TITLE_REGISTRY CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/assets/track-packs/presentation/*.ips"
    "${CMAKE_SOURCE_DIR}/assets/track-packs/presentation/*.txt")
set_property(TARGET FZeroSNESRecomp APPEND PROPERTY LINK_DEPENDS
    "${CMAKE_SOURCE_DIR}/MODS.md" "${CMAKE_SOURCE_DIR}/CONVERSION.md" "${CMAKE_SOURCE_DIR}/docs/PACK_FORMAT.md"
    "${CMAKE_SOURCE_DIR}/docs/FUZEE_FORMAT.md"
    ${FZERO_COURSE_REGISTRY} ${FZERO_VEHICLE_REGISTRY} "${CMAKE_SOURCE_DIR}/mods/README.md" "${CMAKE_SOURCE_DIR}/mods/PARSE_MANIFEST.md"
    ${FZERO_TITLE_REGISTRY})
add_custom_command(TARGET FZeroSNESRecomp POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:FZeroSNESRecomp>/mods/track-packs"
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:FZeroSNESRecomp>/docs"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_BINARY_DIR}/generated/CONVERSION.md" "$<TARGET_FILE_DIR:FZeroSNESRecomp>/mods/CONVERSION.md"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/MODS.md" "${CMAKE_SOURCE_DIR}/CONVERSION.md" "$<TARGET_FILE_DIR:FZeroSNESRecomp>/"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/docs/PACK_FORMAT.md" "${CMAKE_SOURCE_DIR}/docs/FUZEE_FORMAT.md"
        "$<TARGET_FILE_DIR:FZeroSNESRecomp>/docs/"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/mods/README.md" "${CMAKE_SOURCE_DIR}/mods/PARSE_MANIFEST.md"
        "$<TARGET_FILE_DIR:FZeroSNESRecomp>/mods/"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/mods/README.md" "${CMAKE_SOURCE_DIR}/mods/PARSE_MANIFEST.md"
        "$<TARGET_FILE_DIR:FZeroSNESRecomp>/mods/track-packs/"
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:FZeroSNESRecomp>/assets/track-packs"
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:FZeroSNESRecomp>/assets/vehicle-packs"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${FZERO_VEHICLE_REGISTRY}
        "$<TARGET_FILE_DIR:FZeroSNESRecomp>/assets/vehicle-packs"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${FZERO_COURSE_REGISTRY}
        "$<TARGET_FILE_DIR:FZeroSNESRecomp>/assets/track-packs/"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/assets/track-packs/presentation"
        "$<TARGET_FILE_DIR:FZeroSNESRecomp>/assets/track-packs/presentation"
    VERBATIM)
