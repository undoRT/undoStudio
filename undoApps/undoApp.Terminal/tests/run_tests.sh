#!/usr/bin/env bash
# Build and run the terminal tests: the shell under a pty, and the tab bar.
#
# These need ImGui and libvterm but not a GPU or a display. Each test builds
# itself, because what it needs differs: the session is plain C++ over libvterm,
# while the tab bar is drawn inside a real ImGui frame.
#
# Usage:  ./run_tests.sh [name ...]
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
APP="$ROOT/undoApps/undoApp.Terminal"
TESTS="$APP/tests"
VTERM="$ROOT/third_party/libvterm"
OUT="${TMPDIR:-/tmp}/undoStudio-terminal-tests"
mkdir -p "$OUT"

# Names given on the command line filter the run.
wanted() {
  local n="$1"; shift
  [ "$#" -eq 0 ] && return 0
  for pat in "$@"; do [[ "$n" == *"$pat"* ]] && return 0; done
  return 1
}

# libvterm is C and the plugin is C++, so each of its sources is compiled on its
# own and the objects are handed to the link. Building them as part of the
# g++ command line would compile them as C++.
#
# They are built once and reused: both tests link them, and the terminal's own
# CMakeLists lists the same nine files.
vterm_objects() {
  local objs=()
  for src in encoding keyboard mouse parser pen screen state unicode vterm; do
    local obj="$OUT/vterm-$src.o"
    if [ ! -f "$obj" ] || [ "$VTERM/src/$src.c" -nt "$obj" ]; then
      if ! gcc -std=c99 -O1 -I "$VTERM/include" -c "$VTERM/src/$src.c" -o "$obj" \
           2>"$OUT/vterm-$src.build.log"; then
        echo "  BUILD FAIL  libvterm:$src  (see $OUT/vterm-$src.build.log)"
        return 1
      fi
    fi
    objs+=("$obj")
  done
  printf '%s\n' "${objs[@]}"
}

# forkpty lives in libutil on older glibc and in libc on newer ones. Asking the
# linker for -lutil is harmless either way on a system that has it, and a system
# without it is one where libc has it.
pty_libs() {
  if [ -n "${UTIL_LIBRARY:-}" ]; then
    printf '%s\n' "$UTIL_LIBRARY"
  elif ldconfig -p 2>/dev/null | grep -q 'libutil\.so'; then
    printf '%s\n' "-lutil"
  fi
}

# The shell on a pty: libvterm and the session, and no ImGui.
run_session_test() {
  local name="$1"
  local exe="$OUT/$name"
  local work="$OUT/work-$name"

  local vterm_objs
  mapfile -t vterm_objs < <(vterm_objects) || return 1

  mapfile -t pty < <(pty_libs)

  if ! g++ -std=c++17 -O1 \
        -I "$APP/include" -I "$VTERM/include" \
        -o "$exe" "$TESTS/$name.cpp" "$APP/src/TerminalSession.cpp" \
        "${vterm_objs[@]}" "${pty[@]}" \
        2>"$OUT/$name.build.log"; then
    echo "  BUILD FAIL  $name  (see $OUT/$name.build.log)"
    return 1
  fi

  # A working directory of its own: the session starts a shell in one.
  rm -rf "$work"
  mkdir -p "$work"
  ( cd "$work" && "$exe" )
  local rc=$?
  if [ $rc -eq 0 ]; then
    echo "  PASS  $name"
    return 0
  fi
  echo "  FAIL  $name"
  return 1
}

# The tab bar: ImGui compiled in, and the panel's own sources.
run_tabs_test() {
  local name="tabs"
  local exe="$OUT/$name"
  local work="$OUT/work-$name"

  # The core library is needed for the panels TerminalApp registers into, and the
  # newest of the two possible build locations wins, for the reason the core
  # runner's script gives.
  local libdir=""
  local newest=0
  for candidate in "$ROOT/build" "$ROOT"; do
    local lib="$candidate/libundoStudioCore.so"
    [ -f "$lib" ] || continue
    local when
    when=$(stat -c %Y "$lib")
    if [ "$when" -gt "$newest" ]; then
      newest="$when"
      libdir="$candidate"
    fi
  done
  if [ -z "$libdir" ]; then
    echo "  SKIP  $name (build the project first: cmake -B build && cmake --build build)"
    return 0
  fi

  local vterm_objs
  mapfile -t vterm_objs < <(vterm_objects) || return 1
  mapfile -t pty < <(pty_libs)

  if ! g++ -std=c++17 -O1 \
        -I "$APP/include" -I "$ROOT/include" -I "$ROOT/third_party/imgui" \
        -I "$VTERM/include" \
        -o "$exe" "$TESTS/$name.cpp" \
        "$APP/src/TerminalSession.cpp" "$APP/src/TerminalView.cpp" \
        "$APP/src/undoAppTerminal.cpp" \
        "$ROOT/third_party/imgui/imgui.cpp" \
        "${vterm_objs[@]}" \
        -L "$libdir" -lundoStudioCore -lpthread "${pty[@]}" -Wl,-rpath,"$libdir" \
        2>"$OUT/$name.build.log"; then
    echo "  BUILD FAIL  $name  (see $OUT/$name.build.log)"
    return 1
  fi

  rm -rf "$work"
  mkdir -p "$work"
  ( cd "$work" && "$exe" )
  local rc=$?
  if [ $rc -eq 0 ]; then
    echo "  PASS  $name"
    return 0
  fi
  echo "  FAIL  $name"
  return 1
}

echo "== building and running the terminal tests =="
# shellcheck disable=SC2312
failed=0
for t in "$TESTS"/*.cpp; do
  name=$(basename "$t" .cpp)
  wanted "$name" "$@" || continue
  case "$name" in
    tabs) run_tabs_test "$name" || failed=1 ;;
    *)    run_session_test "$name" || failed=1 ;;
  esac
done

if [ $failed -eq 0 ]; then
  echo "== all terminal tests passed =="
else
  echo "== SOME TERMINAL TESTS FAILED =="
fi
exit $failed