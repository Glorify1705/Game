{ pkgs ? import <nixpkgs> {}}:

pkgs.mkShell {
  packages = with pkgs; [
    cmake
    libGL
    clang-tools
    SDL2
    SDL2_mixer
    zlib
    python3
    fnlfmt
  ];
}
