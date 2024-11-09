{ pkgs, lib, config, inputs, ... }:

{

  packages = with pkgs; [ 
    SDL2
    SDL2_mixer
    ccls
    clang
    clang-tools
    cmake
    fnlfmt
    gdb
    libGL
    libGLU
    mesa
    python3
    sqlite
    sqlitebrowser
    xorg.xrandr
    zlib
  ];
  
  env.CCLS_LOCATION = "${pkgs.ccls}/bin/ccls";

  tasks."game:build" = {
    exec = "cmake -S . -B build && cmake --build build --target Game";
  };

  tasks."game:run" = {
    exec = "cmake -S . -B build && cmake --build build --target Run";
  };

  tasks."game:clean" = {
    exec = "rm -rf build/*";
  };
}
