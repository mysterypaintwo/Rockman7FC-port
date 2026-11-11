#!/bin/bash
# ---------------------------------------------------------------------------
# Rockman 7 FC — macOS installer
#
# Double-click, or run in Terminal:
#   "Install Rockman 7 FC.command" [--game path/to/final2] [--english path/to/mm7fc]
#
# Installs the Japanese release (rm7fc.exe) and/or the English release
# (mm7fc.exe) into ~/Library/Application Support/Rockman7FC, and the
# "Rockman 7 FC" and "Rockman 7 FC Settings" apps into /Applications.
# Re-running it updates an existing install and keeps your settings.
# ---------------------------------------------------------------------------
set -u

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PAYLOAD="$HERE/payload"

say()  { printf '%s\n' "$*"; }
pause() { [[ -t 0 ]] && { printf 'Press Return to close…'; read -r _; }; }
fail() { printf '\n!! %s\n\n' "$*"; pause; exit 1; }

# Shared files sit next to this script in a packaged release, or in build/ and
# the repo root when run from the source tree. They are copied into each app's
# Resources folder when the apps are installed.
shared() { # name -> path
  local d
  for d in "$HERE" "$HERE/../build" "$HERE/../build/icons" "$HERE/.."; do
    [[ -f "$d/$1" ]] && { printf '%s' "$d/$1"; return 0; }
  done
  return 1
}

say "==============================================="
say " Rockman 7 FC — macOS port installer"
say "==============================================="
say ""
CORE="$(shared launcher-core.sh)"    || fail "launcher-core.sh not found."
TOOL_EXE="$(shared Rockman7FC.exe)"  || fail "Rockman7FC.exe not found. Run tools/build.sh first."
TOOL_DLL="$(shared pxtone-proxy.dll)" || fail "pxtone-proxy.dll not found. Run tools/build.sh first."
ICNS="$(shared rockman7fc.icns)"     || ICNS=""

JA_SRC=""; EN_SRC=""
while [[ $# -gt 0 ]]; do
  case "$1" in
    --game|--japanese) JA_SRC="${2:-}"; shift 2 ;;
    --english)         EN_SRC="${2:-}"; shift 2 ;;
    *) if [[ -f "$1/mm7fc.exe" ]]; then EN_SRC="$1"; else JA_SRC="$1"; fi; shift ;;
  esac
done

pick_folder() { # prompt
  osascript -e "POSIX path of (choose folder with prompt \"$1\")" 2>/dev/null | sed 's:/*$::'
}

# --- locate the releases ------------------------------------------------------
if [[ -z "$JA_SRC" ]]; then
  for c in "$HERE/final2" "$HERE/../final2" "$HERE/../../final2" "$HERE/.." "$HOME/Downloads/final2"; do
    [[ -f "$c/rm7fc.exe" ]] && { JA_SRC="$(cd "$c" && pwd)"; break; }
  done
fi
if [[ -z "$EN_SRC" ]]; then
  for c in "$HERE/mm7fc" "$HERE/../mm7fc" "$HERE/../../mm7fc" "$HOME/Downloads/mm7fc"; do
    [[ -f "$c/mm7fc.exe" ]] && { EN_SRC="$(cd "$c" && pwd)"; break; }
  done
fi
if [[ -z "$JA_SRC" && -z "$EN_SRC" ]]; then
  say "Where is the game? Pick the folder containing rm7fc.exe (Japanese) or mm7fc.exe (English)."
  p="$(pick_folder "Select a Rockman 7 FC folder (contains rm7fc.exe or mm7fc.exe)")"
  if [[ -f "$p/mm7fc.exe" ]]; then EN_SRC="$p"; elif [[ -f "$p/rm7fc.exe" ]]; then JA_SRC="$p"; fi
fi
SUPPORT="$HOME/Library/Application Support/Rockman7FC"
if [[ -z "$JA_SRC" && -z "$EN_SRC" && ! -d "$SUPPORT/ja" && ! -d "$SUPPORT/en" && ! -d "$SUPPORT/game" ]]; then
  fail "No rm7fc.exe or mm7fc.exe found."
fi
[[ -n "$JA_SRC" ]] && say "Japanese release : $JA_SRC"
[[ -n "$EN_SRC" ]] && say "English release  : $EN_SRC"
[[ -z "$JA_SRC" || -z "$EN_SRC" ]] && \
  say "(Only one release given. The other can be added later with Settings > Add release.)"
say ""

# --- install game data -----------------------------------------------------
PORT_PLATFORM="macos"
PORT_RESOURCES="$(dirname "$TOOL_EXE")"   # install_tools copies the exe + dll from here
PORT_SUPPORT_DIR="$SUPPORT"
# shellcheck disable=SC1091
. "$CORE"
port_install "$JA_SRC" "$EN_SRC" || fail "Installing the game files failed. See $PORT_LOG"

# --- apps --------------------------------------------------------------------
DEST="/Applications"
[[ -w "$DEST" ]] || { DEST="$HOME/Applications"; mkdir -p "$DEST"; }
# apps from older versions of this port
for old in "Rockman 7 FC Config.app" "Rockman 7 FC Language.app"; do
  [[ -d "$DEST/$old" ]] && rm -rf "$DEST/$old" && say "Removed old    : $DEST/$old"
done
for app in "Rockman 7 FC.app" "Rockman 7 FC Settings.app"; do
  rm -rf "$DEST/$app"
  cp -R "$PAYLOAD/$app" "$DEST/$app" || fail "Could not install $app into $DEST"
  res="$DEST/$app/Contents/Resources"
  mkdir -p "$res"
  cp "$CORE" "$TOOL_EXE" "$TOOL_DLL" "$res/" || fail "Could not copy files into $app"
  if [[ -n "$ICNS" ]]; then
    icon="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleIconFile' "$DEST/$app/Contents/Info.plist" 2>/dev/null || echo Rockman7FC)"
    cp "$ICNS" "$res/${icon%.icns}.icns"
  fi
  chmod +x "$DEST/$app/Contents/MacOS/"*
  xattr -dr com.apple.quarantine "$DEST/$app" 2>/dev/null
  touch "$DEST/$app"      # refresh the Finder/Dock icon cache
  say "Installed      : $DEST/$app"
done

# --- Wine ----------------------------------------------------------------------
if WINE_BIN="$(find_wine)"; then
  say "Wine           : $WINE_BIN"
else
  say ""
  say "Wine (the Windows compatibility layer) is not installed yet. Install it with:"
  say "    brew install --cask --no-quarantine gcenx/wine/wine-stable"
  say "(no Homebrew? see https://github.com/Gcenx/macOS_Wine_builds)"
fi

say ""
say "Done. Open \"Rockman 7 FC\" to play, or \"Rockman 7 FC Settings\" to change"
say "window size, language, volume, and keyboard/gamepad controls."
say "Files: $SUPPORT"
say ""
pause
