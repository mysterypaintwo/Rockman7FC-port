#!/bin/bash
# ---------------------------------------------------------------------------
# Rockman 7 FC — Linux installer
#
#   ./install-linux.sh [path/to/final2] [path/to/mm7fc]
#
# Installs the Japanese release (rm7fc.exe) and/or the English release
# (mm7fc.exe) into ~/.local/share/rockman7fc, plus the `rockman7fc` and
# `rockman7fc-settings` launchers and menu entries. Re-running it updates an
# existing install and keeps your settings.
# ---------------------------------------------------------------------------
set -u

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DATA="${XDG_DATA_HOME:-$HOME/.local/share}"
SUPPORT="$DATA/rockman7fc"
BIN="$SUPPORT/bin"

say()  { printf '%s\n' "$*"; }
fail() { printf '\n!! %s\n\n' "$*"; exit 1; }
ask()  { local r; read -r -p "$1 [y/N] " r; [[ "$r" == [yY]* ]]; }

# Shared files sit next to this script in a packaged release, or in build/ and
# the repo root when run from the source tree.
shared() { # name -> path
  local d
  for d in "$HERE" "$HERE/../build" "$HERE/../build/icons" "$HERE/.."; do
    [[ -f "$d/$1" ]] && { printf '%s' "$d/$1"; return 0; }
  done
  fail "$1 not found. Run tools/build.sh first."
}

say "==============================================="
say " Rockman 7 FC — Linux port installer"
say "==============================================="
say ""

JA_SRC=""; EN_SRC=""
for a in "$@"; do
  case "$a" in --english|--game|--japanese) continue ;; esac
  if [[ -f "$a/mm7fc.exe" ]]; then EN_SRC="$a"
  elif [[ -f "$a/rm7fc.exe" ]]; then JA_SRC="$a"
  else fail "No rm7fc.exe or mm7fc.exe in: $a"; fi
done
if [[ -z "$JA_SRC" && -z "$EN_SRC" ]]; then
  for c in "$HERE/final2" "$HERE/../final2" "$HERE/../../final2"; do
    [[ -f "$c/rm7fc.exe" ]] && { JA_SRC="$(cd "$c" && pwd)"; break; }
  done
  for c in "$HERE/mm7fc" "$HERE/../mm7fc" "$HERE/../../mm7fc"; do
    [[ -f "$c/mm7fc.exe" ]] && { EN_SRC="$(cd "$c" && pwd)"; break; }
  done
fi
if [[ -z "$JA_SRC" && -z "$EN_SRC" && ! -d "$SUPPORT/ja" && ! -d "$SUPPORT/en" ]]; then
  read -r -p "Path to the game folder (contains rm7fc.exe or mm7fc.exe): " p
  if [[ -f "$p/mm7fc.exe" ]]; then EN_SRC="$p"; elif [[ -f "$p/rm7fc.exe" ]]; then JA_SRC="$p"
  else fail "No rm7fc.exe or mm7fc.exe in: $p"; fi
fi
[[ -n "$JA_SRC" ]] && say "Japanese release : $JA_SRC"
[[ -n "$EN_SRC" ]] && say "English release  : $EN_SRC"

# --- launchers ---------------------------------------------------------------
mkdir -p "$BIN" "$HOME/.local/bin"
for f in rockman7fc rockman7fc-settings; do
  cp "$HERE/$f" "$BIN/" || fail "Could not copy $f."
done
for f in launcher-core.sh Rockman7FC.exe pxtone-proxy.dll; do
  src="$(shared "$f")" || exit 1
  cp "$src" "$BIN/" || fail "Could not copy $f."
done
chmod +x "$BIN/rockman7fc" "$BIN/rockman7fc-settings"
rm -f "$BIN/rockman7fc-config" "$BIN/rockman7fc-language" \
      "$HOME/.local/bin/rockman7fc-config" "$HOME/.local/bin/rockman7fc-language"
ln -sf "$BIN/rockman7fc"          "$HOME/.local/bin/rockman7fc"
ln -sf "$BIN/rockman7fc-settings" "$HOME/.local/bin/rockman7fc-settings"

# --- game data -----------------------------------------------------------------
PORT_PLATFORM="linux"
PORT_RESOURCES="$BIN"
PORT_SUPPORT_DIR="$SUPPORT"
# shellcheck disable=SC1091
. "$BIN/launcher-core.sh"
port_install "$JA_SRC" "$EN_SRC" || fail "Installing the game files failed. See $PORT_LOG"

# --- menu entries ----------------------------------------------------------------
ICON_DIR="$DATA/icons"; mkdir -p "$ICON_DIR"
src="$(shared rockman7fc.png)" || exit 1
cp "$src" "$ICON_DIR/rockman7fc.png"
APPS_DIR="$DATA/applications"; mkdir -p "$APPS_DIR"
entry() { # file name exec comment
  cat >"$APPS_DIR/$1" <<DESK
[Desktop Entry]
Type=Application
Version=1.0
Name=$2
Comment=$4
Exec=$3
Icon=$ICON_DIR/rockman7fc.png
Terminal=false
Categories=Game;ActionGame;
Keywords=rockman;megaman;fangame;
StartupNotify=false
DESK
  chmod +x "$APPS_DIR/$1"
}
entry rockman7fc.desktop "Rockman 7 FC" "$BIN/rockman7fc" "Mega Man 7 reimagined as an 8-bit game (runs through Wine)"
entry rockman7fc-settings.desktop "Rockman 7 FC Settings" "$BIN/rockman7fc-settings" "Window size, language, volume, keyboard and gamepad controls"
command -v update-desktop-database >/dev/null 2>&1 && update-desktop-database "$APPS_DIR" >/dev/null 2>&1 || true

# --- Wine ----------------------------------------------------------------------
if find_wine >/dev/null; then
  say "Wine         : $(find_wine)"
else
  say ""
  say "No Wine found. The game is a 32-bit Windows program, so it needs one of:"
  say "  Debian/Ubuntu : sudo dpkg --add-architecture i386 && sudo apt update && sudo apt install wine32"
  say "  Fedora        : sudo dnf install wine"
  say "  Arch          : sudo pacman -S wine"
  say "  Or let me download a self-contained portable Wine build (~250 MB)."
  say ""
  if ask "Download portable Wine into $SUPPORT now?"; then
    TMP="$SUPPORT/wine-dl.tar.xz"; URL=""
    if command -v curl >/dev/null 2>&1; then
      URL="$(curl -fsSL --max-time 20 https://api.github.com/repos/Kron4ek/Wine-Builds/releases/latest \
             | grep -o 'https://[^"]*amd64-wow64\.tar\.xz' | head -1)"
    fi
    if [[ -z "$URL" ]]; then
      say "Could not find a download (offline?). Install Wine with your package manager."
    elif curl -fL --progress-bar -o "$TMP" "$URL"; then
      U="$SUPPORT/.unpack"; rm -rf "$U"; mkdir -p "$U"
      tar -xf "$TMP" -C "$U" && rm -rf "$SUPPORT/wine" && \
        mv "$(find "$U" -maxdepth 1 -mindepth 1 -type d | head -1)" "$SUPPORT/wine"
      rm -rf "$U" "$TMP"
      [[ -x "$SUPPORT/wine/bin/wine" ]] && say "Portable Wine installed." || say "Unpacking failed — install Wine manually."
    else
      say "Download failed."
    fi
  fi
fi

say ""
say "Done."
say "Play     :  rockman7fc"
say "Settings :  rockman7fc-settings   (window, language, sound, keyboard + gamepad)"
say "Files    :  $SUPPORT"
say "If ~/.local/bin is not on your PATH, use $BIN/rockman7fc"
