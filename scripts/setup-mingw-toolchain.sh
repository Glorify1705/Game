#!/usr/bin/env bash
# Downloads a pre-built MinGW-w64 GCC toolchain for Linux x86_64 hosts.
# The toolchain is extracted to toolchains/mingw-w64/ in the project root.
#
# On NixOS, all ELF binaries are patched with patchelf to use the Nix
# dynamic linker and library paths (glibc, libstdc++, zlib, etc.).
#
# Source: https://github.com/xpack-dev-tools/mingw-w64-gcc-xpack
set -euo pipefail

VERSION="14.2.0-1"
ARCHIVE="xpack-mingw-w64-gcc-${VERSION}-linux-x64.tar.gz"
URL="https://github.com/xpack-dev-tools/mingw-w64-gcc-xpack/releases/download/v${VERSION}/${ARCHIVE}"

ROOT="${DEVENV_ROOT:-$(cd "$(dirname "$0")/.." && pwd)}"
TOOLCHAIN_DIR="$ROOT/toolchains/mingw-w64"

if [ -x "$TOOLCHAIN_DIR/bin/x86_64-w64-mingw32-g++" ]; then
  echo "MinGW toolchain already installed at $TOOLCHAIN_DIR"
  "$TOOLCHAIN_DIR/bin/x86_64-w64-mingw32-g++" --version | head -1
  exit 0
fi

echo "Downloading MinGW-w64 GCC $VERSION..."
mkdir -p "$ROOT/toolchains"
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

curl -L --progress-bar -o "$TMP/$ARCHIVE" "$URL"

echo "Extracting..."
tar -xzf "$TMP/$ARCHIVE" -C "$TMP"

# The archive extracts to xpack-mingw-w64-gcc-<version>/
mv "$TMP"/xpack-mingw-w64-gcc-*/ "$TOOLCHAIN_DIR"

# On NixOS, pre-built binaries need their ELF interpreter and RPATH patched.
# Detect the Nix dynamic linker from the current shell's interpreter.
PATCHELF="${PATCHELF:-$(command -v patchelf 2>/dev/null || true)}"
if [ -z "$PATCHELF" ]; then
  # patchelf not in PATH — try to get it via nix-shell.
  PATCHELF=$(nix-shell -p patchelf --run 'command -v patchelf' 2>/dev/null) || true
fi
INTERP=""
if [ -n "$PATCHELF" ]; then
  INTERP=$("$PATCHELF" --print-interpreter "$(command -v bash)" 2>/dev/null) || true
fi
if [ -n "$INTERP" ] && [[ "$INTERP" == /nix/store/* ]]; then
  echo "NixOS detected — patching ELF binaries..."

  # Minimal RPATH: only glibc from Nix (for libc.so.6, libm, libdl, etc.)
  # plus the toolchain's own bundled libraries (libexec has zlib, libiconv,
  # libgmp, libmpc, libmpfr, libisl, libzstd, libstdc++, etc.).
  # Do NOT add Nix store zlib/libstdc++ — their DT_NEEDED entries reference
  # libc.so (the glibc linker script, not an ELF), which breaks the loader.
  NIXLIB="$(dirname "$INTERP")"
  for d in "$TOOLCHAIN_DIR"/lib "$TOOLCHAIN_DIR"/lib64 "$TOOLCHAIN_DIR"/libexec \
           "$TOOLCHAIN_DIR"/libexec/gcc/x86_64-w64-mingw32/*/; do
    [ -d "$d" ] && NIXLIB="$NIXLIB:$d"
  done

  patched=0
  failed=0
  while IFS= read -r -d '' elf; do
    # Skip Windows PE files (.exe, .dll, .a, .o) and non-ELF files.
    if file "$elf" | grep -q "ELF.*executable\|ELF.*shared object"; then
      if "$PATCHELF" --set-interpreter "$INTERP" --set-rpath "$NIXLIB" "$elf" 2>/dev/null; then
        patched=$((patched + 1))
      else
        failed=$((failed + 1))
      fi
    fi
  done < <(find "$TOOLCHAIN_DIR" -type f -executable -print0)

  echo "Patched $patched binaries ($failed skipped)."
fi

echo "Installed to $TOOLCHAIN_DIR"
"$TOOLCHAIN_DIR/bin/x86_64-w64-mingw32-g++" --version | head -1
