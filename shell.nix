{ pkgs ? import <nixpkgs> {}}:

pkgs.mkShell {
  packages = with pkgs; [
    SDL2
    SDL2_mixer
    ccls
    clang-tools
    cmake
    fnlfmt
    gdb
    libGL
    python3
    xorg.xrandr
    zlib
  ];
}
