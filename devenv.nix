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
    gperftools
    libGL
    libGLU
    libllvm
    mesa
    ninja
    pprof
    python3
    renderdoc
    sqlite
    sqlitebrowser
    stylua
    valgrind
    xorg.libXcursor
    xorg.libXinerama
    xorg.libXrandr
    xorg.xrandr
  ];

  env.CCLS_LOCATION = "${pkgs.ccls}/bin/ccls";

  enterShell = ''
    export CC="${pkgs.clang}/bin/clang";
    export CXX="${pkgs.clang}/bin/clang++";
  '';

  scripts."game-build" = {
    exec = "cmake -G Ninja -S . -B build && cmake --build build --target Game";
  };

  scripts."game-run" = {
    exec = "cmake -G Ninja -S . -B build && cmake --build build --target Run";
  };

  scripts."game-clean" = {
    exec = "rm -rf build/*";
  };

  scripts."game-test" = {
    exec = ''
      cmake -DCMAKE_BUILD_TYPE=Debug -G Ninja -S . -B build && cmake --build build --target Tests
    '';
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
      ${pkgs.clang-tools}/bin/clang-format -i src/* tests/*
      for f in assets/*.fnl; do ${pkgs.fnlfmt}/bin/fnlfmt --fix "$f"; done
      ${pkgs.stylua}/bin/stylua assets/*.lua
    '';
  };

  scripts."game-debug" = {
    exec = ''
      ${pkgs.gdb}/bin/gdb --args ./build/Game assets ./build/assets.sqlite3
    '';
  };

  git-hooks.hooks = {
    clang-format.enable = true;

		donotsubmit = {
			enable = true;

			name = "DONOTSUBMIT checker";

			entry = "scripts/donotsubmit.sh";
		};
  };
}
