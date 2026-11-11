#!/bin/bash
# ---------------------------------------------------------------------------
# Rockman 7 FC — shared launcher core (macOS + Linux)
#
# Sourced by the platform launchers and installers. All game settings live in
# port.ini and are applied inside the game by the pxtone proxy DLL, and edited
# with Rockman7FC.exe (the settings window) — the same files Windows uses.
# This script only does what is specific to running under Wine:
#   * find Wine, keep a private prefix, install a Japanese-capable font
#   * renderer / desktop-window / gamescope options from port.ini [Wine]
#   * install releases side by side (ja/ = rm7fc.exe, en/ = mm7fc.exe)
#   * migrate the old settings.conf layout
#
# Layout under PORT_SUPPORT_DIR:
#   port.ini  Rockman7FC.exe  pxtone-proxy.dll  launcher.log  prefix/
#   ja/  (Japanese release)   en/  (English release)
# ---------------------------------------------------------------------------

PORT_NAME="Rockman 7 FC"

: "${PORT_SUPPORT_DIR:?PORT_SUPPORT_DIR must be set}"
: "${PORT_RESOURCES:?PORT_RESOURCES must be set}"

PORT_INI="$PORT_SUPPORT_DIR/port.ini"
PORT_LOG="$PORT_SUPPORT_DIR/launcher.log"
WINEPREFIX="$PORT_SUPPORT_DIR/prefix"
export WINEPREFIX

mkdir -p "$PORT_SUPPORT_DIR" 2>/dev/null

# --- logging / errors ------------------------------------------------------
port_log() { printf '%s %s\n' "$(date '+%Y-%m-%d %H:%M:%S')" "$*" >>"$PORT_LOG" 2>/dev/null; }

port_notify() {
  port_log "notify: $*"
  if command -v osascript >/dev/null 2>&1; then
    osascript -e "display notification \"$*\" with title \"$PORT_NAME\"" >/dev/null 2>&1 || true
  elif command -v notify-send >/dev/null 2>&1; then
    notify-send "$PORT_NAME" "$*" >/dev/null 2>&1 || true
  fi
}

port_fatal() {
  port_log "FATAL: $1"
  if command -v osascript >/dev/null 2>&1; then
    osascript -e "display alert \"$PORT_NAME\" message \"$1\" as critical" >/dev/null 2>&1 || true
  elif command -v zenity >/dev/null 2>&1; then
    zenity --error --title="$PORT_NAME" --text="$1" >/dev/null 2>&1 || true
  elif command -v kdialog >/dev/null 2>&1; then
    kdialog --title "$PORT_NAME" --error "$1" >/dev/null 2>&1 || true
  fi
  printf 'error: %s\n' "$1" >&2
  exit 1
}

# --- port.ini --------------------------------------------------------------
# ini_get <section> <key> <default>   (case-insensitive, CRLF-safe)
ini_get() {
  local v
  v="$(awk -v sec="$1" -v key="$2" '
    { sub(/\r$/, "") }
    /^[[:space:]]*\[/ { s = $0; gsub(/[][[:space:]]/, "", s); insec = (tolower(s) == tolower(sec)); next }
    insec {
      i = index($0, "="); if (!i) next
      k = substr($0, 1, i - 1); gsub(/[[:space:]]/, "", k)
      if (tolower(k) == tolower(key)) { val = substr($0, i + 1); sub(/^[[:space:]]+/, "", val); sub(/[[:space:]]+$/, "", val); print val; exit }
    }' "$PORT_INI" 2>/dev/null)"
  printf '%s' "${v:-$3}"
}

# --- find a Wine runtime ---------------------------------------------------
find_wine() {
  local c
  if [[ -n "${ROCKMAN7FC_WINE:-}" && -x "${ROCKMAN7FC_WINE}" ]]; then
    printf '%s\n' "$ROCKMAN7FC_WINE"; return 0
  fi
  for c in "$PORT_RESOURCES/wine/bin/wine" \
           "$PORT_SUPPORT_DIR/wine/bin/wine" \
           "$PORT_RESOURCES/wine/bin/wine64" \
           "$PORT_SUPPORT_DIR/wine/bin/wine64" \
           "$PORT_SUPPORT_DIR"/runtime/*.app/Contents/Resources/wine/bin/wine \
           "$PORT_RESOURCES"/runtime/*.app/Contents/Resources/wine/bin/wine; do
    [[ -x "$c" ]] && { printf '%s\n' "$c"; return 0; }
  done
  for c in "/Applications/Wine Stable.app/Contents/Resources/wine/bin/wine" \
           "/Applications/Wine Staging.app/Contents/Resources/wine/bin/wine" \
           "/Applications/Wine Devel.app/Contents/Resources/wine/bin/wine" \
           "/Applications/Wine Crossover.app/Contents/Resources/wine/bin/wine" \
           "/Applications/CrossOver.app/Contents/SharedSupport/CrossOver/bin/wine" \
           "$HOME/Applications/Wine Stable.app/Contents/Resources/wine/bin/wine"; do
    [[ -x "$c" ]] && { printf '%s\n' "$c"; return 0; }
  done
  for c in wine wine64 wine-stable; do
    if command -v "$c" >/dev/null 2>&1; then command -v "$c"; return 0; fi
  done
  return 1
}

# --- prefix ----------------------------------------------------------------
ensure_prefix() {
  [[ -d "$WINEPREFIX/drive_c" ]] && return 0
  port_log "creating prefix at $WINEPREFIX"
  port_notify "First launch: preparing the Windows environment (about a minute)…"
  WINEDEBUG=-all WINEDLLOVERRIDES="mscoree,mshtml=" \
    "$WINE_BIN" wineboot -u >>"$PORT_LOG" 2>&1 || true
  "$WINE_BIN" wineserver -w >>"$PORT_LOG" 2>&1 || true
  [[ -d "$WINEPREFIX/drive_c" ]] || port_fatal \
    "Could not create the Wine prefix at:
$WINEPREFIX

See the log for details:
$PORT_LOG"
}

# --- Japanese fonts --------------------------------------------------------
# The Japanese build asks for "ＭＳ ゴシック" / "MS Gothic" for window titles and
# dialogs. Wine ships no CJK font, so those render as "?????" without one.
CJK_CANDIDATES_MAC=(
  "$PORT_RESOURCES/fonts/NotoSansJP-Regular.ttf"
  "/System/Library/Fonts/Supplemental/Arial Unicode.ttf"
  "/Library/Fonts/Arial Unicode.ttf"
  "/System/Library/Fonts/ヒラギノ角ゴシック W3.ttc"
  "/System/Library/Fonts/Hiragino Sans GB.ttc"
)
CJK_CANDIDATES_LINUX=(
  "$PORT_RESOURCES/fonts/NotoSansJP-Regular.ttf"
  "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc"
  "/usr/share/fonts/opentype/noto/NotoSansCJKjp-Regular.otf"
  "/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc"
  "/usr/share/fonts/google-noto-cjk/NotoSansCJK-Regular.ttc"
  "/usr/share/fonts/truetype/fonts-japanese-gothic.ttf"
  "/usr/share/fonts/opentype/ipafont-gothic/ipag.ttf"
  "/usr/share/fonts/truetype/vlgothic/VL-Gothic-Regular.ttf"
)
font_family_for() {
  case "$1" in
    *Arial\ Unicode*)              printf 'Arial Unicode MS' ;;
    *ヒラギノ*|*Hiragino*Sans\ GB*) printf 'Hiragino Kaku Gothic ProN' ;;
    *NotoSansCJK*|*NotoSansJP*)    printf 'Noto Sans CJK JP' ;;
    *ipag*)                        printf 'IPAPGothic' ;;
    *VL-Gothic*)                   printf 'VL Gothic' ;;
    *fonts-japanese-gothic*)       printf 'TakaoPGothic' ;;
    *)                             printf 'MS Gothic' ;;
  esac
}

ensure_fonts() {
  local marker="$WINEPREFIX/.rockman7fc-fonts" fontsdir="$WINEPREFIX/drive_c/windows/Fonts"
  [[ -f "$marker" ]] && return 0
  [[ -d "$fontsdir" ]] || return 0
  local cand="" list
  if [[ "${PORT_PLATFORM:-}" == "linux" ]]; then list=("${CJK_CANDIDATES_LINUX[@]}")
  else list=("${CJK_CANDIDATES_MAC[@]}"); fi
  for cand in "${list[@]}"; do [[ -f "$cand" ]] && break; cand=""; done
  if [[ -z "$cand" ]]; then
    port_log "no CJK font found — Japanese window titles will show as \"?????\""
    : >"$marker"
    return 0
  fi
  port_log "installing CJK font: $cand"
  cp -f "$cand" "$fontsdir/" >>"$PORT_LOG" 2>&1 || true
  local fam alias
  fam="$(font_family_for "$cand")"
  for alias in "MS Gothic" "MS PGothic" "MS UI Gothic" "MS Mincho" "MS PMincho" \
               "ＭＳ ゴシック" "ＭＳ Ｐゴシック" "ＭＳ 明朝" "ＭＳ Ｐ明朝" "Terminal"; do
    WINEDEBUG=-all "$WINE_BIN" reg add 'HKCU\Software\Wine\Fonts\Replacements' \
      /v "$alias" /d "$fam" /f >>"$PORT_LOG" 2>&1 || true
  done
  printf '%s\n' "$fam" >"$marker"
  port_log "font alias: MS Gothic -> $fam"
}

# --- controllers -------------------------------------------------------------
# By default Wine hands Switch Pro / DualShock-style pads to games as raw HID,
# whose reports (with a running packet counter) read as phantom button presses
# through the joystick API. With hidraw off, Wine's SDL backend maps every pad
# to a proper XInput controller instead.
ensure_controllers() {
  local marker="$WINEPREFIX/.rockman7fc-hidraw"
  [[ -f "$marker" ]] && return 0
  "$WINE_BIN" reg add 'HKLM\System\CurrentControlSet\Services\winebus' \
    /v DisableHidraw /t REG_DWORD /d 1 /f >>"$PORT_LOG" 2>&1 || true
  "$WINE_BIN" wineserver -k >>"$PORT_LOG" 2>&1 || true   # winebus reads it at start-up
  : >"$marker"
  port_log "controllers: SDL gamepad mapping (winebus DisableHidraw=1)"
}

# --- renderer --------------------------------------------------------------
# Only touches the registry when the setting changes ("reg" takes a second).
apply_renderer() {
  local want marker="$WINEPREFIX/.rockman7fc-renderer" cur=""
  want="$(ini_get Wine Renderer default | tr '[:upper:]' '[:lower:]')"
  [[ "$want" == "gdi" ]] || want="default"
  [[ -f "$marker" ]] && cur="$(cat "$marker")"
  [[ "$cur" == "$want" || ( -z "$cur" && "$want" == "default" ) ]] && return 0
  if [[ "$want" == "gdi" ]]; then
    "$WINE_BIN" reg add 'HKCU\Software\Wine\Direct3D' /v DirectDrawRenderer /d gdi /f >>"$PORT_LOG" 2>&1 || true
  else
    "$WINE_BIN" reg delete 'HKCU\Software\Wine\Direct3D' /v DirectDrawRenderer /f >>"$PORT_LOG" 2>&1 || true
  fi
  printf '%s' "$want" >"$marker"
  port_log "renderer: $want"
}

# --- releases & proxy ------------------------------------------------------
release_exe() { [[ -f "$1/mm7fc.exe" ]] && echo mm7fc.exe || echo rm7fc.exe; }

# Folder of an installed release for ja|en, or nothing.
release_dir() {
  local exe=rm7fc.exe
  [[ "$1" == "en" ]] && exe=mm7fc.exe
  [[ -f "$PORT_SUPPORT_DIR/$1/$exe" ]] && printf '%s' "$PORT_SUPPORT_DIR/$1"
}

is_proxy() { LC_ALL=C grep -aq pxtone_real "$1" 2>/dev/null; }

# Installs Rockman7FC.exe + pxtone-proxy.dll next to port.ini.
install_tools() {
  local f src
  for f in Rockman7FC.exe pxtone-proxy.dll; do
    src=""
    for c in "$PORT_RESOURCES/$f" "$PORT_RESOURCES/../$f"; do [[ -f "$c" ]] && { src="$c"; break; }; done
    [[ -n "$src" ]] || continue
    cmp -s "$src" "$PORT_SUPPORT_DIR/$f" || cp -f "$src" "$PORT_SUPPORT_DIR/$f"
  done
}

# ensure_proxy <game dir>: original pxtone.dll -> pxtone_real.dll, proxy in.
ensure_proxy() {
  local d="$1" proxy="$PORT_SUPPORT_DIR/pxtone-proxy.dll"
  if [[ ! -f "$d/pxtone_real.dll" ]]; then
    [[ -f "$d/pxtone.dll" ]] || { port_log "proxy: no pxtone.dll in $d"; return 1; }
    if is_proxy "$d/pxtone.dll"; then
      port_log "proxy: $d has only the proxy, original pxtone.dll missing"
      return 1
    fi
    mv "$d/pxtone.dll" "$d/pxtone_real.dll" || return 1
    port_log "proxy: kept original as $d/pxtone_real.dll"
  fi
  if [[ -f "$proxy" ]] && ! cmp -s "$proxy" "$d/pxtone.dll"; then
    cp -f "$proxy" "$d/pxtone.dll" && port_log "proxy: installed in $d"
  fi
}

# copy_release <source folder> -> prints ja|en
copy_release() {
  local src="${1%/}" lang dst
  if [[ -f "$src/rm7fc.exe" ]]; then lang=ja
  elif [[ -f "$src/mm7fc.exe" ]]; then lang=en
  else return 1; fi
  dst="$PORT_SUPPORT_DIR/$lang"
  mkdir -p "$dst"
  if command -v rsync >/dev/null 2>&1; then
    # port/ keeps the pristine sound effects; everything else mirrors the source
    rsync -a --delete --exclude '.DS_Store' --exclude 'Thumbs.db' --exclude 'backtrace*.txt' \
          --filter 'P port/' --filter 'P port.log' "$src"/ "$dst"/ >>"$PORT_LOG" 2>&1 || return 1
  else
    cp -a "$src"/. "$dst"/ || return 1
    rm -f "$dst"/backtrace*.txt "$dst/.DS_Store" "$dst/pxtone_real.dll"
  fi
  chmod -R u+w "$dst"
  rm -f "$dst/pxtone_real.dll" 2>/dev/null   # re-derived from the fresh pxtone.dll
  ensure_proxy "$dst" || return 1
  printf '%s' "$lang"
}

write_default_ini() {
  [[ -f "$PORT_INI" ]] && return 0
  local lang=ja
  [[ -n "$(release_dir en)" ]] && lang=en
  printf '[Display]\r\nScale=3\r\nLanguage=%s\r\n' "$lang" >"$PORT_INI"
  port_log "wrote default port.ini (language $lang)"
}

# --- migration from the settings.conf layout ------------------------------
migrate_old_layout() {
  local old="$PORT_SUPPORT_DIR/settings.conf" g="$PORT_SUPPORT_DIR/game"
  if [[ -f "$old" && ! -f "$PORT_INI" ]]; then
    local get
    get() { sed -n "s/^$1=//p" "$old" | tail -1 | tr -d '"\r'; }
    local scale lang bgm se bv sv ka kj vd rd
    scale="$(get WINDOW_SCALE)"; lang="$(get LANGUAGE)"; bgm="$(get BGM)"; se="$(get SE)"
    bv="$(get BGM_VOLUME)"; sv="$(get SE_VOLUME)"; ka="$(get KEY_ATTACK)"; kj="$(get KEY_JUMP)"
    vd="$(get USE_VIRTUAL_DESKTOP)"; rd="$(get RENDERER)"
    {
      printf '[Display]\r\nScale=%s\r\nLanguage=%s\r\n' "${scale:-3}" "${lang:-ja}"
      printf '[Sound]\r\nMusic=%s\r\nMusicVolume=%s\r\nEffects=%s\r\nEffectsVolume=%s\r\n' \
             "${bgm:-1}" "${bv:-100}" "${se:-1}" "${sv:-100}"
      printf '[Keys]\r\n'
      [[ "$kj" =~ ^[0-9]+$ && "$kj" != 90 ]] && printf 'Jump=VK%s\r\n' "$kj"
      [[ "$ka" =~ ^[0-9]+$ && "$ka" != 88 ]] && printf 'Shoot=VK%s\r\n' "$ka"
      printf '[Wine]\r\nVirtualDesktop=%s\r\nRenderer=%s\r\n' "${vd:-0}" "${rd:-default}"
    } >"$PORT_INI"
    mv -f "$old" "$old.old"
    [[ -f "$PORT_SUPPORT_DIR/controls.ini" ]] && mv -f "$PORT_SUPPORT_DIR/controls.ini" "$PORT_SUPPORT_DIR/controls.ini.old"
    port_log "migrated settings.conf -> port.ini"
  fi
  if [[ -d "$g" && -f "$g/rm7fc.exe" && ! -d "$PORT_SUPPORT_DIR/ja" ]]; then
    # Undo the old in-place changes: translated art sheets, rescaled SE, proxy.
    [[ -d "$PORT_SUPPORT_DIR/lang/base" ]] && cp -R "$PORT_SUPPORT_DIR/lang/base/." "$g/"
    [[ -d "$PORT_SUPPORT_DIR/se_original" ]] && cp -f "$PORT_SUPPORT_DIR/se_original/"*.wav "$g/se/" 2>/dev/null
    if [[ -f "$g/pxtone_real.dll" ]]; then mv -f "$g/pxtone_real.dll" "$g/pxtone.dll"; fi
    rm -f "$g/audio.cfg" "$g/audio.log" "$g/.port-language"
    if [[ -f "$g/mm7fc/mm7fc.exe" && ! -d "$PORT_SUPPORT_DIR/en" ]]; then
      mv "$g/mm7fc" "$PORT_SUPPORT_DIR/en"
      ensure_proxy "$PORT_SUPPORT_DIR/en"
    fi
    rm -rf "$g/port"
    mv "$g" "$PORT_SUPPORT_DIR/ja"
    ensure_proxy "$PORT_SUPPORT_DIR/ja"
    rm -rf "$PORT_SUPPORT_DIR/lang" "$PORT_SUPPORT_DIR/se_original" "$PORT_SUPPORT_DIR/tools" \
           "$PORT_SUPPORT_DIR/.se-volume" "$PORT_SUPPORT_DIR/pxtone-proxy.dll.old"
    port_log "migrated game/ -> ja/"
  fi
}

# port_install <folder> [folder]   (used by the installers)
port_install() {
  migrate_old_layout
  install_tools
  local src lang rc=0
  for src in "$@"; do
    [[ -n "$src" ]] || continue
    if lang="$(copy_release "$src")"; then
      port_log "installed $lang release from $src"
      printf 'Installed %s release from %s\n' "$lang" "$src"
    else
      printf '!! Could not install the release in %s (see %s)\n' "$src" "$PORT_LOG"
      rc=1
    fi
  done
  write_default_ini
  return $rc
}

# --- main ------------------------------------------------------------------
port_main() {
  local action="${1:-play}"
  case "$action" in play|settings) ;; *) port_fatal "Unknown action: $action" ;; esac
  port_log "--- launch action=$action platform=${PORT_PLATFORM:-unknown}"

  migrate_old_layout
  install_tools

  WINE_BIN="$(find_wine)" || port_fatal \
"This game is a Windows program, so this port runs it through Wine, and no Wine
installation was found.

macOS:  brew install --cask --no-quarantine gcenx/wine/wine-stable
Linux:  install your distribution's wine package (needs 32-bit support),
        e.g.  sudo apt install wine32        (Debian/Ubuntu)
              sudo dnf install wine           (Fedora)
              sudo pacman -S wine             (Arch)

Then launch this app again. Details: $PORT_LOG"
  port_log "wine: $WINE_BIN"

  export WINEDEBUG=-all
  export WINEDLLOVERRIDES="mscoree,mshtml="
  export MVK_CONFIG_LOG_LEVEL=0          # keep MoltenVK's info dump out of the log
  [[ "${PORT_PLATFORM:-}" == "linux" ]] && export PULSE_LATENCY_MSEC=60

  ensure_prefix
  ensure_fonts
  ensure_controllers

  if [[ "$action" == "settings" ]]; then
    [[ -f "$PORT_SUPPORT_DIR/Rockman7FC.exe" ]] || port_fatal "Rockman7FC.exe is missing from $PORT_SUPPORT_DIR. Re-run the installer."
    cd "$PORT_SUPPORT_DIR" || port_fatal "Cannot enter $PORT_SUPPORT_DIR"
    port_log "exec settings window"
    local rc=0
    "$WINE_BIN" Rockman7FC.exe >>"$PORT_LOG" 2>&1 || rc=$?
    port_log "settings window closed (code $rc)"
    [[ $rc -eq 10 ]] || exit 0          # 10 = the Play button
    action=play
  fi

  # play
  local lang dir
  lang="$(ini_get Display Language ja | tr '[:upper:]' '[:lower:]')"
  [[ "$lang" == "en" ]] || lang=ja
  dir="$(release_dir "$lang")"
  if [[ -z "$dir" ]]; then
    local other=en; [[ "$lang" == "en" ]] && other=ja
    dir="$(release_dir "$other")"
    [[ -n "$dir" ]] && port_log "language $lang not installed, using $other"
  fi
  [[ -n "$dir" ]] || port_fatal "No game files found in:
$PORT_SUPPORT_DIR

Run the installer that came with this port, or open the Settings app and use
\"Add release…\"."
  ensure_proxy "$dir" || port_fatal "The game's audio/controls DLL could not be set up in:
$dir

Re-run the installer. Details: $PORT_LOG"
  apply_renderer

  local exe scale winexe
  exe="$(release_exe "$dir")"
  # Full Windows path: explorer /desktop cannot resolve a bare file name.
  winexe="Z:$(printf '%s' "$dir/$exe" | tr '/' '\\')"
  scale="$(ini_get Display Scale 3)"
  [[ "$scale" =~ ^[0-9]+$ && "$scale" -ge 1 && "$scale" -le 16 ]] || scale=3
  # Japanese ANSI code page so the Japanese build's strings decode correctly.
  if [[ "$lang" == "ja" && "${LANG:-}" != ja_JP* ]]; then
    export LANG=ja_JP.UTF-8 LC_ALL=ja_JP.UTF-8
  fi
  cd "$dir" || port_fatal "Cannot enter $dir"

  if [[ "$(ini_get Wine Gamescope 0)" == "1" ]] && command -v gamescope >/dev/null 2>&1; then
    local w=$((256*scale)) h=$((224*scale))
    port_log "exec gamescope ${w}x${h} $exe"
    exec gamescope -f -S integer -w "$w" -h "$h" -- "$WINE_BIN" "$winexe"
  fi
  if [[ "$(ini_get Wine VirtualDesktop 0)" == "1" && "$(ini_get Display Fullscreen 0)" == "0" ]]; then
    local size
    size="$(ini_get Wine DesktopSize "")"
    [[ "$size" =~ ^[0-9]+x[0-9]+$ ]] || size="$((256*scale+64))x$((224*scale+96))"
    port_log "exec wine explorer /desktop=Rockman7FC,$size $winexe"
    exec "$WINE_BIN" explorer "/desktop=Rockman7FC,$size" "$winexe"
  fi
  port_log "exec wine $winexe"
  exec "$WINE_BIN" "$winexe"
}
