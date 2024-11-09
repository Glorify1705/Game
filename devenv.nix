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

  scripts."game-build" = {
    exec = "cmake -S . -B build && cmake --build build --target Game";
  };

  scripts."game-run" = {
    exec = "cmake -S . -B build && cmake --build build --target Run";
  };

  scripts."game-clean" = {
    exec = "rm -rf build/*";
  };

  scripts."game-db" = {
    exec = "${pkgs.sqlitebrowser}/bin/sqlitebrowser build/assets.sqlite3";
  };

  enterShell = ''
    exec zsh
  '';
}
