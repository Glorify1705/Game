{ pkgs ? import <nixpkgs> {}}:

pkgs.mkShell.override { stdenv = pkgs.clangStdenv; } {
  packages = with pkgs; [
    SDL2
    SDL2_mixer
    ccls
    clang-tools
    clang
    cmake
    fnlfmt
    gdb
    libGL
    python3
    xorg.xrandr
    zlib
  ];

  shellHook= ''
    export CCLS_LOCATION="${pkgs.ccls}/bin/ccls"
  '';
}
