{ pkgs, lib, config, inputs, ... }:

let
  # Nix clang wrapper injects include paths that standalone tools (clang-tidy,
  # clang-include-cleaner) don't see. Extract these paths and pass them explicitly.
  #
  # The script runs clang++ -v to discover the wrapper-injected paths at
  # configure time (via find_program), so store paths update automatically.
  nixClangExtraArgs = pkgs.writeText "nix-clang-extra-args" (
    let
      gccPkg = pkgs.gcc.cc;
      gccVer = gccPkg.version;
      gccInstallDir = "${gccPkg}/lib/gcc/x86_64-unknown-linux-gnu/${gccVer}";
    in ''--gcc-install-dir=${gccInstallDir}
-idirafter
${pkgs.glibc.dev}/include''
  );

  wrappedClangTidy = pkgs.writeShellScriptBin "clang-tidy" ''
    EXTRA_ARGS=()
    while IFS= read -r line; do
      EXTRA_ARGS+=(--extra-arg="$line")
    done < ${nixClangExtraArgs}
    exec ${pkgs.clang-tools}/bin/clang-tidy "''${EXTRA_ARGS[@]}" "$@"
  '';

  wrappedIncludeCleaner = pkgs.writeShellScriptBin "clang-include-cleaner" ''
    EXTRA_ARGS=()
    while IFS= read -r line; do
      EXTRA_ARGS+=(--extra-arg="$line")
    done < ${nixClangExtraArgs}
    exec ${pkgs.clang-tools}/bin/clang-include-cleaner "''${EXTRA_ARGS[@]}" "$@"
  '';
in

{

  packages = with pkgs; [
    SDL3
    ccls
    clang
    clang-tools
    cmake
    ffmpeg
    fnlfmt
    gdb
    gf
    gperftools
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
    wrappedClangTidy
    wrappedIncludeCleaner
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
      cmake -DENABLE_CLANG_TIDY=ON -G Ninja -S . -B build && cmake --build build --target Game
    '';
  };

  scripts."game-include-cleaner" = {
    exec = ''
      cmake -G Ninja -S . -B build > /dev/null 2>&1
      EXTRA_ARGS=()
      while IFS= read -r dir; do
        [ -n "$dir" ] && EXTRA_ARGS+=(--extra-arg="-isystem$dir")
      done < build/implicit_include_dirs.txt
      for f in src/*.cc; do
        clang-include-cleaner --print=changes --disable-insert -p build "''${EXTRA_ARGS[@]}" "$f" 2>/dev/null
      done
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

  };
}
