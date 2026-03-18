{ pkgs, lib, config, inputs, ... }:

{

  packages = with pkgs; [ 
    SDL2
    SDL2_mixer
    ccls
    clang
    clang-tools
    cmake
    ffmpeg
    fnlfmt
    gdb
    gf
    gperftools
    include-what-you-use
    libGL
    libGLU
    libllvm
    lua-language-server
    lua51Packages.fennel
    lua51Packages.lua
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
      ${pkgs.gf}/bin/gf2 --args ./build/Game assets ./build/assets.sqlite3
    '';
  };

  scripts."game-tidy" = {
    exec = ''
      cmake -G Ninja -S . -B build
      EXTRA_ARGS=""
      for p in $(${pkgs.clang}/bin/clang++ -v -x c++ /dev/null -fsyntax-only 2>&1 | grep -oP '(?<=-cxx-isystem )\S+'); do
        EXTRA_ARGS="$EXTRA_ARGS -extra-arg=-isystem$p"
      done
      for p in $(${pkgs.clang}/bin/clang++ -v -x c++ /dev/null -fsyntax-only 2>&1 | grep -oP '(?<=-idirafter )\S+'); do
        EXTRA_ARGS="$EXTRA_ARGS -extra-arg=-isystem$p"
      done
      run-clang-tidy -p build -quiet $EXTRA_ARGS '/src/[^/]+\.cc$'
    '';
  };

  scripts."game-iwyu" = {
    exec = ''
      cmake -G Ninja -S . -B build
      ${pkgs.python3}/bin/python3 ${pkgs.include-what-you-use}/bin/iwyu_tool.py -p build src/ -- -Xiwyu --mapping_file=iwyu.imp
    '';
  };

  scripts."game-sanitize" = {
    exec = ''
      cmake -DENABLE_SANITIZERS=ON -DCMAKE_BUILD_TYPE=Debug -G Ninja -S . -B build && cmake --build build --target Game
    '';
  };

  git-hooks.hooks = {
    clang-format.enable = true;

		donotsubmit = {
			enable = true;

			name = "DONOTSUBMIT checker";

			entry = "scripts/donotsubmit.sh";
		};

		clang-tidy-hook = {
			enable = true;

			name = "clang-tidy";

			entry = "scripts/run-clang-tidy.sh";
		};

		iwyu-hook = {
			enable = true;

			name = "include-what-you-use";

			entry = "scripts/run-iwyu.sh";
		};
  };
}
