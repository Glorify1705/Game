# CMake toolchain file for cross-compiling to WebAssembly using Emscripten.
#
# Usage:
#   game-build-web              # inside the devenv shell (nixpkgs emscripten)
# or
#   cmake --preset web && cmake --build --preset web
#
# Locates emcc from the environment (the devenv shell provides
# pkgs.emscripten) or from an emsdk checkout via $EMSDK, then defers to
# Emscripten's own platform toolchain file.

find_program(_EMCC emcc)

if(_EMCC)
  # nixpkgs layout: <root>/bin/emcc with the platform file under
  # <root>/share/emscripten/cmake/Modules/Platform/.
  get_filename_component(_EM_BIN "${_EMCC}" DIRECTORY)
  get_filename_component(_EM_ROOT "${_EM_BIN}/.." ABSOLUTE)
  set(_EM_TOOLCHAIN
      "${_EM_ROOT}/share/emscripten/cmake/Modules/Platform/Emscripten.cmake")
  if(NOT EXISTS "${_EM_TOOLCHAIN}")
    # emsdk layout: emcc lives next to the cmake directory.
    set(_EM_TOOLCHAIN "${_EM_BIN}/cmake/Modules/Platform/Emscripten.cmake")
  endif()
elseif(DEFINED ENV{EMSDK})
  set(_EM_TOOLCHAIN
      "$ENV{EMSDK}/upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake")
endif()

if(NOT _EM_TOOLCHAIN OR NOT EXISTS "${_EM_TOOLCHAIN}")
  message(FATAL_ERROR
    "Emscripten not found.\n"
    "Enter the devenv shell (which provides emscripten), or install emsdk "
    "and set the EMSDK environment variable.")
endif()

include("${_EM_TOOLCHAIN}")
