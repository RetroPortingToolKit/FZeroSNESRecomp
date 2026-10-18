#!/usr/bin/env bash
# test_appimage_layout.sh <AppDir>
set -eu

if [ "$#" -ne 1 ]; then
  echo "usage: $0 /path/to/AppDir" >&2
  exit 2
fi

APPDIR="$(CDPATH= cd -- "$1" && pwd)"
test -x "$APPDIR/AppRun" || { echo "missing executable AppRun" >&2; exit 1; }
test -x "$APPDIR/usr/bin/F-Zero Forever" || { echo "missing F-Zero Forever ELF" >&2; exit 1; }
test -d "$APPDIR/usr/bin/assets" || { echo "missing launcher assets" >&2; exit 1; }
test -f "$APPDIR/usr/bin/assets/shaders/crt-soft.glslp" || { echo "missing CRT shader" >&2; exit 1; }
test -f "$APPDIR/usr/share/doc/fzerosnesrecomp/README.md" || { echo "missing user instructions" >&2; exit 1; }
for notice in snesrecomp recomp-ui imgui "${SNESRECOMP_SDL_BACKEND:-SDL3}"; do
  test -f "$APPDIR/usr/share/doc/fzerosnesrecomp/licenses/$notice.txt" || {
    echo "missing $notice license notice" >&2; exit 1; }
done

if [ -d "$APPDIR/usr/bin/mods" ]; then
  # The payload ships inside the binary, not beside it, so a download can
  # never be missing it; the files here are credits and provenance only.
  grep -qa BSDELX1 "$APPDIR/usr/bin/F-Zero Forever" || {
    echo "binary has no embedded BS Deluxe payload" >&2; exit 1; }
  test -f "$APPDIR/usr/bin/mods/BS-Deluxe-credits.txt" || { echo "missing BS Deluxe credits" >&2; exit 1; }
fi
if [ -d "$APPDIR/usr/bin/patches" ]; then
  test -f "$APPDIR/usr/bin/patches/bs-deluxe-usa.ips" || { echo "missing BS Deluxe IPS patch" >&2; exit 1; }
fi

# --- the file picker must not be poisoned by the bundle ---------------------
# AppRun must NOT export a bundle LD_LIBRARY_PATH. It is inherited by every
# child process, including the host zenity/kdialog the launcher spawns for the
# ROM picker; that child then loads OUR glib/pcre2 against the host GTK it is
# linked against and dies on start. In 1.6.0 that made "Browse For ROM" do
# nothing at all. The binary carries RUNPATH $ORIGIN/../lib and does not need
# the variable.
if grep -Eq '^[[:space:]]*export[[:space:]]+LD_LIBRARY_PATH=' "$APPDIR/AppRun"; then
  echo "AppRun exports a global LD_LIBRARY_PATH; it leaks into spawned host tools" >&2
  grep -n 'LD_LIBRARY_PATH' "$APPDIR/AppRun" >&2
  exit 1
fi
grep -q 'RECOMP_HOST_LD_LIBRARY_PATH' "$APPDIR/AppRun" || {
  echo "AppRun does not record the host LD_LIBRARY_PATH for spawned tools" >&2
  exit 1
}

# Everything the binary needs must resolve with LD_LIBRARY_PATH unset -- that
# is exactly what the shipped AppRun gives the loader.
if env -u LD_LIBRARY_PATH ldd "$APPDIR/usr/bin/F-Zero Forever" | grep -q 'not found'; then
  echo "binary has unresolved libraries without LD_LIBRARY_PATH" >&2
  env -u LD_LIBRARY_PATH ldd "$APPDIR/usr/bin/F-Zero Forever" | grep 'not found' >&2
  exit 1
fi

# ...and nothing may sit in usr/lib that the loader will not actually choose.
# An unreachable copy is only ever reachable by setting the global
# LD_LIBRARY_PATH above, so leaving one there invites the bug back.
if [ -d "$APPDIR/usr/lib" ]; then
  REACH="$(env -u LD_LIBRARY_PATH ldd "$APPDIR/usr/bin/F-Zero Forever" \
            | awk '{ for (i = 1; i <= NF; i++) if ($i ~ /^\//) print $i }' \
            | while read -r p; do readlink -f "$p" 2>/dev/null || true; done \
            | sort -u)"
  for f in "$APPDIR"/usr/lib/*; do
    [ -e "$f" ] || continue
    real="$(readlink -f "$f")"
    printf '%s\n' "$REACH" | grep -qxF "$real" || {
      echo "unreachable library bundled in usr/lib: $(basename "$f")" >&2
      echo "(nothing loads it without a global LD_LIBRARY_PATH)" >&2
      exit 1
    }
  done
fi

TMP="$(mktemp -d)"
trap 'chmod -R u+w "$APPDIR" "$TMP" 2>/dev/null || true; rm -rf "$TMP"' EXIT HUP INT TERM
chmod -R a-w "$APPDIR"

STATE="$TMP/state"
mkdir -p "$STATE"
: > "$STATE/pretend.sfc"
( cd "$STATE" && APPIMAGE="$STATE/F-Zero-Forever.AppImage" \
    SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
    timeout 20 "$APPDIR/AppRun" >/dev/null 2>&1 ) || true

test -f "$STATE/rom.cfg" || { echo "adjacent ROM did not seed rom.cfg" >&2; exit 1; }
test "$(head -n1 "$STATE/rom.cfg")" = "$STATE/pretend.sfc" || {
  echo "rom.cfg points at the wrong ROM" >&2
  exit 1
}
if [ -d "$APPDIR/usr/bin/patches" ]; then
  test -f "$STATE/patches/bs-deluxe-usa.ips" || {
    echo "AppRun did not expose the BS Deluxe IPS patch beside the AppImage" >&2
    exit 1
  }
fi

for leak in rom.cfg keybinds.ini fzero-video.ini saves last_run_report.json; do
  found="$(find "$APPDIR" -name "$leak" -print -quit)"
  [ -z "$found" ] || { echo "state leaked into AppDir: $found" >&2; exit 1; }
done

# --- the picker, exercised through the real AppRun --------------------------
# RECOMP_UI_PICKER_SELFTEST makes the shipped binary run one native pick and
# report the tri-state outcome, so this runs the REAL AppRun environment
# against a stub backend without anyone clicking Browse.
STUBBIN="$TMP/stubbin"
REC="$TMP/picker.env"
mkdir -p "$STUBBIN"
cat > "$STUBBIN/zenity" <<STUB
#!/bin/sh
/usr/bin/env > "$REC"
exit \${STUB_ZENITY_EXIT:-1}
STUB
chmod +x "$STUBBIN/zenity"

picker_run() {  # $1 = stub exit code -> stdout of the run
  rm -f "$REC"
  PICKDIR="$TMP/pick$1"
  mkdir -p "$PICKDIR"
  : > "$PICKDIR/pretend.sfc"
  ( cd "$PICKDIR" && APPIMAGE="$PICKDIR/F-Zero-Forever.AppImage" \
      PATH="$STUBBIN:$PATH" STUB_ZENITY_EXIT="$1" \
      RECOMP_UI_PICKER_SELFTEST=1 \
      SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
      SNESRECOMP_AUTOCLOSE_FRAMES=1 \
      timeout 40 "$APPDIR/AppRun" 2>&1 ) || true
}

# 1. The backend really was reached, and it was NOT handed the bundle's
#    library path. This is the root cause of the 1.6.0 bug.
OUT1="$(picker_run 1)"
printf '%s\n' "$OUT1" | grep -q '\[picker-selftest\] native_available=1' || {
  echo "picker self-test did not see the stub backend" >&2
  printf '%s\n' "$OUT1" | tail -20 >&2
  exit 1
}
test -f "$REC" || { echo "stub picker backend never ran" >&2; exit 1; }
if grep -q '^LD_LIBRARY_PATH=' "$REC"; then
  echo "spawned picker backend inherited LD_LIBRARY_PATH:" >&2
  grep '^LD_LIBRARY_PATH=' "$REC" >&2
  exit 1
fi
for poison in LD_PRELOAD APPDIR APPIMAGE; do
  grep -q "^$poison=" "$REC" && {
    echo "spawned picker backend inherited $poison" >&2; exit 1; }
done
# exit 1 is a genuine cancel: no fallback, and no path.
printf '%s\n' "$OUT1" | grep -q '\[picker-selftest\] result=0' || {
  echo "backend exit 1 was not read as a cancel" >&2
  printf '%s\n' "$OUT1" | grep picker-selftest >&2
  exit 1
}
printf '%s\n' "$OUT1" | grep -q '\[picker-selftest\] builtin_fallback=no' || {
  echo "a cancel must not fall back to the built-in picker" >&2
  exit 1
}

# 2. Any other exit code means the backend is unusable -> the built-in picker
#    takes over. In 1.6.0 this was mapped to "cancel" and the click did nothing.
OUT2="$(picker_run 2)"
printf '%s\n' "$OUT2" | grep -q '\[picker-selftest\] result=-1' || {
  echo "backend exit 2 was not reported as unusable" >&2
  printf '%s\n' "$OUT2" | grep picker-selftest >&2
  exit 1
}
printf '%s\n' "$OUT2" | grep -q '\[picker-selftest\] builtin_fallback=yes' || {
  echo "a failed backend did not fall back to the built-in picker" >&2
  printf '%s\n' "$OUT2" | grep picker-selftest >&2
  exit 1
}
printf '%s\n' "$OUT2" | grep -q '\[launcher\] file picker: zenity exited 2' || {
  echo "no stderr line naming the backend it skipped and why" >&2
  printf '%s\n' "$OUT2" | grep -i 'file picker' >&2
  exit 1
}

echo "AppImage layout test passed"
