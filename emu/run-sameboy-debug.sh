#!/usr/bin/env bash
set -euo pipefail
REPO_ROOT=$(cd "$(dirname "$0")/.." && pwd)
SDL_PREFIX="$REPO_ROOT/emu/.deps/sdl2"
BIN=$(ls "$REPO_ROOT"/emu/SameBoy-*/build/bin/SDL/sameboy 2>/dev/null | head -1)
ROM="${1:-$REPO_ROOT/build/gbc/gbc-rog.gbc}"
if [[ ! -x "$BIN" ]]; then
  echo "SameBoy not built. Run: make sameboy  or  ./emu/build-sameboy.sh" >&2
  exit 1
fi
if [[ ! -f "$ROM" ]]; then
  echo "ROM not found: $ROM (build with: make)" >&2
  exit 1
fi
export LD_LIBRARY_PATH="$SDL_PREFIX/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
cd "$(dirname "$BIN")"
exec ./sameboy --stop-debugger "$ROM"
