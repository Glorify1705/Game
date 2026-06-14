cmake_minimum_required(VERSION 3.28...4.1)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS True)

option(ENABLE_SANITIZERS "Build with ASan+UBSan" OFF)
option(ENABLE_CLANG_TIDY "Run clang-tidy during build" OFF)
option(ENABLE_PROFILING "Enable trace profiler instrumentation" OFF)
option(ENABLE_IMGUI "Build with Dear ImGui debug UI" ON)

# Auto-detect ccache/sccache for faster rebuilds.
find_program(CCACHE_PROGRAM ccache)
if(NOT CCACHE_PROGRAM)
  find_program(CCACHE_PROGRAM sccache)
endif()
if(CCACHE_PROGRAM)
  set(CMAKE_C_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
  set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
  message(STATUS "Using compiler cache: ${CCACHE_PROGRAM}")
endif()

project(
  Game
  VERSION 0.1
  DESCRIPTION "The game"
  LANGUAGES C CXX)

# Enable Objective-C for macOS (SDL3's Cocoa backend requires it).
if(APPLE)
  enable_language(OBJC)
endif()

# Detect MSVC-frontend compilers (cl.exe and clang-cl). clang-cl's -Wall
# maps to -Weverything, so it needs MSVC-style flags exclusively.
if(CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC"
   OR CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
  set(_MSVC_FRONTEND TRUE)
else()
  set(_MSVC_FRONTEND FALSE)
endif()

# GNU-frontend compilers: GCC, Clang, AppleClang (excludes clang-cl).
set(IS_GCC_LIKE
    $<AND:$<OR:$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>,$<CXX_COMPILER_ID:GNU>>,$<NOT:$<BOOL:${_MSVC_FRONTEND}>>>
)

if(ENABLE_CLANG_TIDY)
    find_program(CLANG_TIDY_EXE NAMES clang-tidy REQUIRED)
    # Nix wraps clang with implicit -isystem paths that CMake detects and strips
    # from compile commands. clang-tidy doesn't use the Nix wrapper, so re-add them.
    set(CLANG_TIDY_CMD "${CLANG_TIDY_EXE}")
    foreach(dir IN LISTS CMAKE_CXX_IMPLICIT_INCLUDE_DIRECTORIES)
        list(APPEND CLANG_TIDY_CMD "--extra-arg=-isystem${dir}")
    endforeach()
endif()

# Third-party libraries (SYSTEM suppresses warnings from their headers).
set(BOX2D_BUILD_TESTBED OFF CACHE BOOL "" FORCE)
set(BOX2D_BUILD_UNIT_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(libraries/box2d EXCLUDE_FROM_ALL SYSTEM)

add_subdirectory(libraries/lua EXCLUDE_FROM_ALL SYSTEM)
add_subdirectory(libraries/physfs EXCLUDE_FROM_ALL SYSTEM)

set(MI_OVERRIDE OFF CACHE BOOL "" FORCE)
set(MI_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(MI_BUILD_OBJECT OFF CACHE BOOL "" FORCE)
set(MI_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(libraries/mimalloc EXCLUDE_FROM_ALL SYSTEM)

set(BUILD_REGRESS OFF)
set(BUILD_EXAMPLES OFF)
set(BUILD_DOC OFF)
set(BUILD_TOOLS OFF)
set(PHYSFS_DISABLE_INSTALL ON CACHE BOOL "" FORCE)
set(PHYSFS_BUILD_TEST OFF CACHE BOOL "" FORCE)
set(PHYSFS_BUILD_DOCS OFF CACHE BOOL "" FORCE)

if(NOT WIN32)
  find_package(OpenGL REQUIRED)
endif()

set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_DISABLE_INSTALL ON CACHE BOOL "" FORCE)
set(SDL_ALSA_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_PULSEAUDIO_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_X11_SHARED OFF CACHE BOOL "" FORCE)
add_subdirectory(libraries/SDL3 EXCLUDE_FROM_ALL SYSTEM)

if(ENABLE_IMGUI)
    add_subdirectory(libraries/imgui EXCLUDE_FROM_ALL SYSTEM)
    add_subdirectory(libraries/text_editor EXCLUDE_FROM_ALL SYSTEM)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  find_library(LIBDW_LIBRARY dw)
  find_path(LIBDW_INCLUDE_DIR NAMES "elfutils/libdw.h")
  if(LIBDW_LIBRARY AND LIBDW_INCLUDE_DIR)
    set(LIBDW_FOUND TRUE)
    message(STATUS "Found libdw: ${LIBDW_LIBRARY}")
  else()
    set(LIBDW_FOUND FALSE)
    message(STATUS "libdw not found, stack traces will show addresses only")
  endif()
endif()

# Write Nix-implicit include dirs for standalone tools (clang-include-cleaner).
# CMake strips these from compile_commands.json, so tools need them explicitly.
string(REPLACE ";" "\n" _implicit_dirs "${CMAKE_CXX_IMPLICIT_INCLUDE_DIRECTORIES}")
file(WRITE "${CMAKE_BINARY_DIR}/implicit_include_dirs.txt" "${_implicit_dirs}\n")

find_path(VALGRIND_INCLUDE_DIR valgrind/memcheck.h)

configure_file(src/version.h.in version.h)

# Vendored single-file libraries. Compiled as a static library so they are
# only rebuilt when their own sources change, not on every engine edit.
add_library(vendored STATIC
    libraries/double-conversion/bignum.cc
    libraries/double-conversion/bignum-dtoa.cc
    libraries/double-conversion/cached-powers.cc
    libraries/double-conversion/double-to-string.cc
    libraries/double-conversion/fast-dtoa.cc
    libraries/double-conversion/fixed-dtoa.cc
    libraries/double-conversion/string-to-double.cc
    libraries/double-conversion/strtod.cc
    libraries/dr_wav.cc
    libraries/glad.cc
    libraries/sqlite3.c
    libraries/stb_rect_pack.cc
    libraries/stb_truetype.cc
    libraries/stb_image_write.cc
    libraries/stb_image_all.cc
    libraries/stb_vorbis.cc
    libraries/yyjson.c
    libraries/backward.cc
    libraries/enet.cc)

target_include_directories(vendored PUBLIC "${PROJECT_SOURCE_DIR}")
target_compile_features(vendored PRIVATE cxx_std_17)
set_target_properties(vendored PROPERTIES SKIP_LINTING ON)

target_compile_options(vendored PRIVATE
    $<${IS_GCC_LIKE}:-Wall;-Wextra;-Wno-error;-Wno-unused-parameter>
    $<$<AND:${IS_GCC_LIKE},$<CONFIG:Debug>>:-O1;-fno-omit-frame-pointer;-fno-optimize-sibling-calls;-fno-inline>
    $<$<BOOL:${_MSVC_FRONTEND}>:/W3>
)
if(NOT MSVC)
  set_source_files_properties(libraries/stb_image_write.cc PROPERTIES
      COMPILE_OPTIONS "-Wno-missing-field-initializers")
endif()
set_source_files_properties(libraries/sqlite3.c PROPERTIES
    COMPILE_DEFINITIONS SQLITE_ENABLE_MEMSYS5)
set_source_files_properties(libraries/lua-protobuf/pb.c PROPERTIES
    COMPILE_DEFINITIONS "PB_STATIC_API"
    COMPILE_OPTIONS "-Wno-error")

if(ENABLE_SANITIZERS)
  target_compile_options(vendored PRIVATE
      $<${IS_GCC_LIKE}:-fsanitize=address,undefined;-fno-sanitize=vptr>)
  target_link_options(vendored PRIVATE
      $<${IS_GCC_LIKE}:-fsanitize=address,undefined>)
endif()

# Embed protoc.lua as a C string constant for compile-time proto compilation.
add_custom_command(
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/protoc_lua.h"
    COMMAND "${CMAKE_COMMAND}"
        -DINPUT="${PROJECT_SOURCE_DIR}/libraries/lua-protobuf/protoc.lua"
        -DOUTPUT="${CMAKE_CURRENT_BINARY_DIR}/protoc_lua.h"
        -P "${PROJECT_SOURCE_DIR}/cmake/EmbedProtoc.cmake"
    DEPENDS "${PROJECT_SOURCE_DIR}/libraries/lua-protobuf/protoc.lua"
    COMMENT "Embedding protoc.lua")

# Engine static library. All engine code except main().
set(ENGINE_SRCS
    src/assets.cc
    src/camera.cc
    src/clock.cc
    src/collision.cc
    src/collision_world.cc
    src/color.cc
    src/config.cc
    src/console.cc
    src/file_watcher.cc
    src/filesystem.cc
    src/engine.cc
    src/hot_reload.cc
    src/image.cc
    src/input.cc
    src/logging.cc
    src/lua.cc
    src/lua_stubs.cc
    src/lua_assets.cc
    src/lua_bytebuffer.cc
    src/lua_camera.cc
    src/lua_collision.cc
    src/lua_filesystem.cc
    src/lua_graphics.cc
    src/lua_input.cc
    src/lua_json.cc
    src/lua_physics.cc
    src/lua_log.cc
    src/lua_random.cc
    src/lua_sound.cc
    src/lua_system.cc
    src/lua_test.cc
    src/lua_scene.cc
    src/lua_timer.cc
    src/lua_math.cc
    src/physics.cc
    src/profiler.cc
    src/qoa.cc
    src/renderer.cc
    src/sdl_init.cc
    src/shaders.cc
    src/sound.cc
    src/stats.cc
    src/string_table.cc
    src/stringlib.cc
    src/timer.cc
    src/executor.cc
    src/zone_stats.cc
    src/physics_debug_draw.cc
    src/transformations.cc
    src/xml.cc
    src/packer.cc
    src/cmd_atlas.cc
    src/cmd_clean.cc
    src/cmd_completions.cc
    src/cmd_convert.cc
    src/cmd_help.cc
    src/cli_metadata.cc
    src/cmd_init.cc
    src/cmd_package.cc
    src/cmd_run.cc
    src/cmd_stubs.cc
    src/cmd_version.cc
    src/network.cc
    src/lua_data.cc
    src/lua_network.cc
    src/lua_particles.cc
    src/lua_save.cc
    src/lua_tilemap.cc
    src/particles.cc
    src/save.cc
    src/tilemap.cc
    src/platform.cc
    $<$<BOOL:${ENABLE_IMGUI}>:src/debug_ui.cc>
    libraries/lua-protobuf/pb.c)

add_library(engine STATIC ${ENGINE_SRCS} "${CMAKE_CURRENT_BINARY_DIR}/protoc_lua.h")

target_compile_features(engine PUBLIC cxx_std_17)

target_include_directories(engine PUBLIC
    "${CMAKE_CURRENT_BINARY_DIR}"
    "${PROJECT_SOURCE_DIR}"
    "${PROJECT_SOURCE_DIR}/libraries/lua/src")

if(VALGRIND_INCLUDE_DIR)
    target_include_directories(engine SYSTEM PRIVATE "${VALGRIND_INCLUDE_DIR}")
endif()

target_compile_options(engine PRIVATE
    $<${IS_GCC_LIKE}:-Wall;-Wextra;-Werror;-fno-exceptions;-fno-rtti;-Wshadow;-Wformat=2;-Wimplicit-fallthrough>
    $<$<AND:${IS_GCC_LIKE},$<CONFIG:Debug>>:-O1;-fno-omit-frame-pointer;-fno-optimize-sibling-calls;-fno-inline>
    $<$<BOOL:${_MSVC_FRONTEND}>:/W3;/EHs-c-;/GR->
)

if(ENABLE_SANITIZERS)
  target_compile_options(engine PRIVATE
      $<${IS_GCC_LIKE}:-fsanitize=address,undefined;-fno-sanitize=vptr>)
  target_link_options(engine PRIVATE
      $<${IS_GCC_LIKE}:-fsanitize=address,undefined>)
endif()

target_compile_definitions(engine PUBLIC
    $<$<OR:$<CONFIG:Debug>,$<CONFIG:RelWithDebInfo>>:GAME_WITH_ASSERTS>
    $<$<BOOL:${WIN32}>:NOMINMAX;WIN32_LEAN_AND_MEAN;_USE_MATH_DEFINES>
)

if(LIBDW_FOUND)
  target_compile_definitions(engine PRIVATE BACKWARD_HAS_DW=1)
  # Force the include path even if CMake considers it "implicit" (Nix strips
  # implicit -isystem flags from compile commands, but backward.h needs dwarf.h).
  target_compile_options(engine PRIVATE "SHELL:-isystem ${LIBDW_INCLUDE_DIR}")
endif()

if(ENABLE_PROFILING)
    target_compile_definitions(engine PRIVATE GAME_WITH_PROFILING)
endif()

if(ENABLE_IMGUI)
    target_compile_definitions(engine PUBLIC GAME_WITH_IMGUI)
    target_link_libraries(engine PUBLIC imgui text_editor)
endif()

if(ENABLE_CLANG_TIDY)
    set_target_properties(engine PROPERTIES CXX_CLANG_TIDY "${CLANG_TIDY_CMD}")
endif()

target_link_libraries(engine PUBLIC
    vendored
    SDL3::SDL3
    physfs-static
    box2d
    lua
    mimalloc-static
    $<$<BOOL:${WIN32}>:opengl32>
    $<$<NOT:$<BOOL:${WIN32}>>:OpenGL::GL>
    $<$<BOOL:${LIBDW_FOUND}>:${LIBDW_LIBRARY}>
    $<$<BOOL:${WIN32}>:dbghelp>
    $<$<BOOL:${WIN32}>:ws2_32;winmm>)

# Game executable (just the entry point).
add_executable(Game src/game.cc)
set_target_properties(Game PROPERTIES OUTPUT_NAME "game")
target_link_libraries(Game PRIVATE engine)

target_compile_options(Game PRIVATE
    $<${IS_GCC_LIKE}:-Wall;-Wextra;-Werror;-fno-exceptions;-fno-rtti>
    $<$<AND:${IS_GCC_LIKE},$<CONFIG:Debug>>:-O1;-fno-omit-frame-pointer;-fno-optimize-sibling-calls;-fno-inline>
    $<$<BOOL:${_MSVC_FRONTEND}>:/W3;/EHs-c-;/GR->
)

if(ENABLE_SANITIZERS)
  target_compile_options(Game PRIVATE
      $<${IS_GCC_LIKE}:-fsanitize=address,undefined;-fno-sanitize=vptr>)
  target_link_options(Game PRIVATE
      $<${IS_GCC_LIKE}:-fsanitize=address,undefined>)
endif()

if(WIN32)
  add_custom_command(
    TARGET Game
    POST_BUILD
    COMMAND
      "${CMAKE_COMMAND}" -E copy_if_different
      "$<TARGET_FILE:SDL3::SDL3>" "$<TARGET_FILE_DIR:Game>"
    VERBATIM)
endif()

# Dedicated headless server — Lua + ENet + protobuf, no SDL/GL.
add_executable(Server src/server.cc libraries/enet.cc libraries/lua-protobuf/pb.c
    "${CMAKE_CURRENT_BINARY_DIR}/protoc_lua.h")
set_target_properties(Server PROPERTIES OUTPUT_NAME "server")
target_compile_features(Server PRIVATE cxx_std_17)
target_include_directories(Server PRIVATE
    "${CMAKE_CURRENT_BINARY_DIR}"
    "${PROJECT_SOURCE_DIR}"
    "${PROJECT_SOURCE_DIR}/libraries/lua/src")
target_compile_definitions(Server PRIVATE PB_STATIC_API)
target_compile_options(Server PRIVATE
    $<${IS_GCC_LIKE}:-Wall;-Wextra;-fno-exceptions;-fno-rtti>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>)
set_source_files_properties(libraries/lua-protobuf/pb.c PROPERTIES
    COMPILE_OPTIONS "-Wno-error")
target_link_libraries(Server PRIVATE
    lua
    physfs-static
    $<$<BOOL:${WIN32}>:ws2_32;winmm>)

# Tests link the engine library directly instead of recompiling sources.
if(NOT CMAKE_CROSSCOMPILING)
  enable_testing()

  add_subdirectory(libraries/googletest SYSTEM)

  add_executable(Tests
      tests/test_allocators.cc
      tests/test_containers.cc
      tests/test_strings.cc
      tests/test_math.cc
      tests/test_error.cc
      tests/test_collision.cc
      tests/test_executor.cc
      tests/test_platform.cc
      tests/test_transformations.cc
      tests/test_particles.cc
      tests/test_save.cc
      tests/test_bits.cc
      tests/test_color.cc
      tests/test_stringlib.cc
      tests/test_stats.cc
      tests/test_xml.cc
      tests/test_qoa.cc
  )

  target_compile_features(Tests PRIVATE cxx_std_17)

  target_compile_options(Tests PRIVATE
      $<${IS_GCC_LIKE}:-Wall;-Wextra;-Werror>
      $<$<BOOL:${_MSVC_FRONTEND}>:/W3>
      $<${IS_GCC_LIKE}:-fsanitize=address,undefined;-fno-sanitize=vptr>
  )

  target_include_directories(Tests PRIVATE
      "${PROJECT_SOURCE_DIR}/src"
      "${PROJECT_SOURCE_DIR}/tests"
  )

  target_link_libraries(Tests PRIVATE engine GTest::gtest_main GTest::gmock)
  target_link_options(Tests PRIVATE $<${IS_GCC_LIKE}:-fsanitize=address,undefined>)

  include(GoogleTest)
  gtest_discover_tests(Tests
      DISCOVERY_TIMEOUT 10
      PROPERTIES
          TIMEOUT 30
          LABELS "unit"
  )
endif()
