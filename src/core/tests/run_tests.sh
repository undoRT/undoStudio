#!/usr/bin/env bash
# Build and run the core tests: the state files, and the recent projects list.
#
# These need ImGui but not a GPU. They create a context with a font atlas and
# drive real frames, the way the Editor's own tests do, and those live beside
# their undoApp with their own script.
#
# Each test builds itself, because what it needs differs: the state files are
# plain C++ over the core, while the list needs ImGui compiled in and the core
# library linked for the parts of it that are not in this directory.
#
# Usage:  ./run_tests.sh
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
TESTS="$ROOT/src/core/tests"
OUT="${TMPDIR:-/tmp}/undoStudio-core-tests"
mkdir -p "$OUT"

# Names given on the command line filter the run, as in the Editor's script.
wanted() {
  local n="$1"; shift
  [ "$#" -eq 0 ] && return 0
  for pat in "$@"; do [[ "$n" == *"$pat"* ]] && return 0; done
  return 1
}

# The state files, over the core alone: no ImGui, no graphics library.
run_state_test() {
  local name="$1"
  local exe="$OUT/$name"
  local work="$OUT/work-$name"

  if ! g++ -std=c++17 -O1 -I "$ROOT/include" -o "$exe" \
        "$TESTS/$name.cpp" "$ROOT/src/core/Settings.cpp" "$ROOT/src/core/ProjectManager.cpp" \
        "$ROOT/src/core/OpenTarget.cpp" "$ROOT/src/core/RecentFiles.cpp" \
        2>"$OUT/$name.build.log"; then
    echo "  BUILD FAIL  $name  (see $OUT/$name.build.log)"
    return 1
  fi

  # A directory of its own: these tests are about files written relative to the
  # working directory, and one that picked up the developer's own would be
  # testing their session rather than the code.
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

# The tests that draw inside an ImGui frame. imgui.cpp is compiled in rather than
# linked: the static library belongs to the undoStudio executable's build and is
# not something a test should need built.
# The call sites are read as text: no compilation, and no ImGui either. It is
# passed the repository root so that it can be pointed at another tree.
run_sites_test() {
  local name="$1"
  local exe="$OUT/$name"
  local work="$OUT/work-$name"

  if ! g++ -std=c++17 -O1 -o "$exe" "$TESTS/$name.cpp" \
        2>"$OUT/$name.build.log"; then
    echo "  BUILD FAIL  $name  (see $OUT/$name.build.log)"
    return 1
  fi

  rm -rf "$work"
  mkdir -p "$work"
  ( cd "$ROOT" && "$exe" "$ROOT" )
  local rc=$?
  if [ $rc -eq 0 ]; then
    echo "  PASS  $name"
    return 0
  fi
  echo "  FAIL  $name"
  return 1
}

run_recents_test() {
  local name="$1"
  local exe="$OUT/$name"
  local work="$OUT/work-$name"

  # Both build layouts are looked for, since an in-source build leaves the library
  # in the repository root and an out-of-source one under build/, and a tree can
  # have both after switching between them. The most recently written one wins: a
  # library left over from a build that has not been run since is older than the
  # one the project was just built with, and linking it fails on symbols that
  # exist in the source and not in it.
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

  if ! g++ -std=c++17 -O1 -I "$ROOT/include" -I "$ROOT/third_party/imgui" -o "$exe" \
        "$TESTS/$name.cpp" "$ROOT/third_party/imgui/imgui.cpp" \
        -L "$libdir" -lundoStudioCore -Wl,-rpath,"$libdir" \
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

echo "== building and running the core tests =="
failed=0
for t in "${TESTS}"/*.cpp; do
  name=$(basename "$t" .cpp)
  wanted "$name" "$@" || continue
  case "$name" in
    recents|recents_reload|popup_dismissal|open_file_request|recents_ui|recent_files_ui) run_recents_test "$name" || failed=1 ;;
    popup_call_sites|shipped_layout) run_sites_test "$name" || failed=1 ;;
    *)       run_state_test "$name" || failed=1 ;;
  esac
done

if [ $failed -eq 0 ]; then
  echo "== all core tests passed =="
else
  echo "== SOME CORE TESTS FAILED =="
fi
exit $failed
