{ pkgs ? import <nixpkgs> {}}:

pkgs.mkShell.override { stdenv = pkgs.clangStdenv; } {
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
    python3
    sqlite
    sqlitebrowser
    xorg.xrandr
    zlib
  ];

  shellHook= ''
    export CCLS_LOCATION="${pkgs.ccls}/bin/ccls"
  '';
}
