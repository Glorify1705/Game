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

  languages.cplusplus.enable = true;
  
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

  scripts."game-open-db" = {
    exec = "${pkgs.sqlitebrowser}/bin/sqlitebrowser build/assets.sqlite3";
  };

  scripts."game-reset-db" = {
    exec = ''
      rm -rf build/assets.sqlite3
      ${pkgs.sqlite}/bin/sqlite3 build/assets.sqlite3 < src/schema.sql
    '';
  };

  scripts."game-format" = {
    exec = ''
      ${pkgs.clang-tools}/bin/clang-format -i src/*
    '';
  };
}
