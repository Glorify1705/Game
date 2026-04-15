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
    alsa-lib
    ccls
    clang
    clang-tools
    elfutils
    cmake
    file
    ffmpeg
    fnlfmt
    gdb
    gf
    gperftools
    libGL
    libGLU
    libpulseaudio
    libllvm
    lua-language-server
    lua51Packages.fennel
    lua51Packages.lua
    mesa
    ninja
    patchelf
    python3
    renderdoc
    samply
    sqlite
    sqlitebrowser
    stylua
    p7zip
    valgrind
    wineWowPackages.stable
    wrappedClangTidy
    wrappedIncludeCleaner
    libxkbcommon
    xorg.libX11
    xorg.libxcb
    xorg.libXcursor
    xorg.libXext
    xorg.libXfixes
    xorg.libXi
    xorg.libXinerama
    xorg.libXrandr
    xorg.libXrender
    xorg.libXScrnSaver
    xorg.libXtst
    xorg.xrandr

    # MinGW cross-compilation toolchain is installed separately (not via Nix)
    # due to a nixpkgs GCC 15 build bug. See scripts/setup-mingw-toolchain.sh.
  ];

  env.CCLS_LOCATION = "${pkgs.ccls}/bin/ccls";

  enterShell = ''
    export CC="${pkgs.clang}/bin/clang";
    export CXX="${pkgs.clang}/bin/clang++";
    export CMAKE_PREFIX_PATH="${pkgs.elfutils.dev}:${pkgs.elfutils.out}:$CMAKE_PREFIX_PATH";
    export PATH="$DEVENV_ROOT/build:$PATH";
  '';

  # Each script is a thin wrapper that execs the corresponding file under
  # scripts/. Keeping the bodies out of devenv.nix means editing a script
  # doesn't force a full shell re-evaluation.
  scripts."game-build".exec            = ''exec "$DEVENV_ROOT/scripts/game-build.sh" "$@"'';
  scripts."game-run".exec              = ''exec "$DEVENV_ROOT/scripts/game-run.sh" "$@"'';
  scripts."game-clean".exec            = ''exec "$DEVENV_ROOT/scripts/game-clean.sh" "$@"'';
  scripts."game-test".exec             = ''exec "$DEVENV_ROOT/scripts/game-test.sh" "$@"'';
  scripts."game-open-db".exec          = ''exec "$DEVENV_ROOT/scripts/game-open-db.sh" "$@"'';
  scripts."game-reset-db".exec         = ''exec "$DEVENV_ROOT/scripts/game-reset-db.sh" "$@"'';
  scripts."game-format".exec           = ''exec "$DEVENV_ROOT/scripts/game-format.sh" "$@"'';
  scripts."game-profile".exec          = ''exec "$DEVENV_ROOT/scripts/game-profile.sh" "$@"'';
  scripts."game-debug".exec            = ''exec "$DEVENV_ROOT/scripts/game-debug.sh" "$@"'';
  scripts."game-tidy".exec             = ''exec "$DEVENV_ROOT/scripts/game-tidy.sh" "$@"'';
  scripts."game-include-cleaner".exec  = ''exec "$DEVENV_ROOT/scripts/game-include-cleaner.sh" "$@"'';
  scripts."game-sanitize".exec         = ''exec "$DEVENV_ROOT/scripts/game-sanitize.sh" "$@"'';
  scripts."game-samply".exec           = ''exec "$DEVENV_ROOT/scripts/game-samply.sh" "$@"'';
  scripts."game-build-win64".exec      = ''exec "$DEVENV_ROOT/scripts/game-build-win64.sh" "$@"'';

  git-hooks.hooks = {
    clang-format = {
      enable = true;
      excludes = [ "^libraries/SDL3/" ];
    };

		donotsubmit = {
			enable = true;

			name = "DONOTSUBMIT checker";

			entry = "scripts/donotsubmit.sh";

			excludes = [ "^libraries/SDL3/" ];
		};

		clang-tidy-hook = {
			enable = true;

			name = "clang-tidy";

			entry = "scripts/run-clang-tidy.sh";

			excludes = [ "^libraries/SDL3/" ];
		};

  };
}
