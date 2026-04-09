#!/usr/bin/env bash
set -euo pipefail

# Build SameBoy SDL frontend with vendored RGBDS + SDL2 under emu/.deps/ (no apt packages).
# Run from repo root: ./emu/build-sameboy.sh   or: make sameboy

REPO_ROOT=$(cd "$(dirname "$0")/.." && pwd)
EMU="$REPO_ROOT/emu"
DEPS="$EMU/.deps"
RGBDS_TAR="$DEPS/rgbds-linux-x86_64.tar.xz"
SDL_TAR="$DEPS/SDL2-2.30.10.tar.gz"
SDL_SRC="$DEPS/SDL2-2.30.10"
SDL_PREFIX="$DEPS/sdl2"
SDL_BUILD="$DEPS/sdl2-build"
STUBS="$EMU/pkgconfig-stubs"

mkdir -p "$DEPS"
export PATH="$DEPS:$SDL_PREFIX/bin:${PATH:-}"

if [[ ! -x "$DEPS/rgbasm" ]]; then
  echo "Fetching RGBDS..."
  curl -fL -o "$RGBDS_TAR" "https://github.com/gbdev/rgbds/releases/download/v1.0.1/rgbds-linux-x86_64.tar.xz"
  tar -xf "$RGBDS_TAR" -C "$DEPS"
fi

if [[ ! -x "$SDL_PREFIX/bin/sdl2-config" ]]; then
  echo "Building SDL2 into $SDL_PREFIX (one-time, a few minutes)..."
  if [[ ! -f "$SDL_TAR" ]]; then
    curl -fL -o "$SDL_TAR" "https://github.com/libsdl-org/SDL/releases/download/release-2.30.10/SDL2-2.30.10.tar.gz"
  fi
  if [[ ! -d "$SDL_SRC" ]]; then
    tar -xf "$SDL_TAR" -C "$DEPS"
  fi
  cmake -S "$SDL_SRC" -B "$SDL_BUILD" -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$SDL_PREFIX" -DSDL_SHARED=ON -DSDL_STATIC=OFF
  cmake --build "$SDL_BUILD" -j"$(nproc)"
  cmake --install "$SDL_BUILD"
fi

shopt -s nullglob
sameboy_dirs=("$EMU"/SameBoy-*/)
if ((${#sameboy_dirs[@]} != 1)); then
  echo "Expected exactly one emu/SameBoy-* directory, found ${#sameboy_dirs[@]}." >&2
  exit 1
fi
SAMEBOY_DIR=${sameboy_dirs[0]%/}

export PKG_CONFIG_PATH="$STUBS:$SDL_PREFIX/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
export LD_LIBRARY_PATH="$SDL_PREFIX/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

make -C "$SAMEBOY_DIR" sdl CONF=release ENABLE_OPENAL=0

BIN="$SAMEBOY_DIR/build/bin/SDL/sameboy"
echo "Built: $BIN"
echo "Run (from that directory or with resources path):"
echo "  cd $(dirname "$BIN") && LD_LIBRARY_PATH=$SDL_PREFIX/lib ./sameboy /path/to/game.gbc"
