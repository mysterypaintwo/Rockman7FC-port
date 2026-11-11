#!/bin/bash
# ---------------------------------------------------------------------------
# Builds the Windows binaries and icons into build/.
#
#   tools/build.sh [path/to/rm7fc.exe]
#
# Needs i686-w64-mingw32-gcc (brew install mingw-w64 / apt install mingw-w64).
# The icon is taken from the game executable (default: ../final2/rm7fc.exe or
# $RM7FC_EXE); macOS also needs `iconutil` for the .icns.
# ---------------------------------------------------------------------------
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
B="$ROOT/build"
CC=i686-w64-mingw32-gcc
WINDRES=i686-w64-mingw32-windres
mkdir -p "$B"

EXE="${1:-${RM7FC_EXE:-}}"
if [[ -z "$EXE" ]]; then
  for c in "$ROOT/../final2/rm7fc.exe" "$HOME/Downloads/final2/rm7fc.exe" "$ROOT/../mm7fc/mm7fc.exe"; do
    [[ -f "$c" ]] && { EXE="$c"; break; }
  done
fi
if [[ -n "$EXE" && -f "$EXE" ]]; then
  python3 -I "$ROOT/tools/make-icons.py" "$EXE" "$B/icons"
  if command -v iconutil >/dev/null 2>&1; then
    iconutil -c icns "$B/icons/rockman7fc.iconset" -o "$B/icons/rockman7fc.icns"
  fi
elif [[ ! -f "$B/icons/rockman7fc.ico" ]]; then
  echo "need the game exe once to build the icons: tools/build.sh path/to/rm7fc.exe" >&2
  exit 1
fi

# src/common is compiled into both programs; --gc-sections drops what one of
# them doesn't use.
SRC="$ROOT/src"
CFLAGS="-O2 -std=c99 -Wall -Wextra -Wno-cast-function-type -I$SRC/common
        -ffunction-sections -Wl,--gc-sections
        -s -static-libgcc -Wl,--no-insert-timestamp"
$CC $CFLAGS -shared -o "$B/pxtone-proxy.dll" "$SRC"/proxy/*.c "$SRC"/common/*.c "$SRC/proxy/pxtone_proxy.def" \
    -luser32 -lgdi32
( cd "$SRC/settings" && $WINDRES settings.rc -O coff -o "$B/settings.res" )
$CC $CFLAGS -mwindows -o "$B/Rockman7FC.exe" "$SRC"/settings/*.c "$SRC"/common/*.c "$B/settings.res" \
    -lgdiplus -lgdi32 -luser32 -lshell32 -lole32 -lwinmm -lsetupapi -lhid
echo "built: $B/pxtone-proxy.dll $B/Rockman7FC.exe"
# The installers in windows/, linux/ and macos/ read these files straight from
# build/ (and launcher-core.sh from the repo root), so nothing is copied.
