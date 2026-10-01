#!/usr/bin/env bash
# Build and run the headless tests for the ST editor plugin.
#
# The tests need no GPU: they create an ImGui context with a font atlas and
# drive real frames. They reach STApp internals through a `#define private
# public` trick, which is why every test includes the system headers *before*
# that define (see the top of any test file).
#
# The sources the tests share are compiled once, into an archive, by
# tests/Makefile. They used to be recompiled by every test, which is what the
# suite's runtime used to be made of.
#
# Usage:  ./run_tests.sh            build and run everything
#         ./run_tests.sh t decl     run only the tests whose name contains t/decl
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
TESTS="$ROOT/undoApps/undoApp.Editor/tests"
OUT="${TMPDIR:-/tmp}/undoStudio-tests"
mkdir -p "$OUT"
ARCHIVE="$OUT/libplugintest.a"

# The include paths for a test's own translation unit. The shared sources are
# listed, and compiled, by tests/Makefile, which is what the archive is built from.
INC=(-I "$ROOT/undoApps/undoApp.Editor/include"
     -I "$ROOT/third_party/st2cpp/include"
     -I "$ROOT/third_party/ImGuiColorTextEdit"
     -I "$ROOT/third_party/imgui"
     -I "$ROOT/third_party/nlohmann_json/single_include"
     -I "$ROOT/include")

# UI tests additionally need the ImGui/ImGuiManager libraries. Those live in the
# build tree; the plugin links them, so reuse the same artefacts. Both layouts
# are looked for, since an out-of-source build puts them under build/ and an
# in-source one leaves them in the root.
LIBS=(libimgui.a libimplot.a libundoStudioCore.so)

# Tests that only exercise the UI-free semantic layer.
PURE=(semantic_tokens declaration_index member_access output_filter open_documents)

# Tests that drive ImGui, so they need the ImGui/ImGuiManager libraries too. Kept
# out of PURE on purpose: output_filter_persistence is the only one that clicks
# things, and adding it to PURE would compile it without the libraries and fail to
# link, which reads as a missing archive rather than a missing list entry.

# Inspection programs, not tests. They print what the editor produced and contain
# no check() calls, so they can neither pass nor fail; they were reported as FAIL
# only because they never print the "all checks passed" line the suite looks for.
# They are still built and run, so their output stays available, but they are
# reported separately and do not decide the exit status.
DIAGNOSTICS=(e2e_diagnostics fb_shadowing project_registry)

wanted() {
  [ "$#" -eq 0 ] && return 0
  local n="$1"; shift
  [ "$#" -eq 0 ] && return 0
  for pat in "$@"; do [[ "$n" == *"$pat"* ]] && return 0; done
  return 1
}

build_archive() {
  # The sources every test needs, compiled once instead of once per test.
  #
  # This used to be the whole cost of the suite: each of the seventeen tests
  # recompiled the plugin and the twenty-eight st2cpp sources of its own, about
  # thirty thousand lines, in sequence. Compiling them into an archive once and
  # linking against it is the difference between eight minutes and well under
  # one, and nothing else about the tests changes: a translation unit compiled
  # here is the same unit the single g++ call used to compile.
  #
  # The rules live in tests/Makefile, which make also reads the header
  # dependencies from: a header edit has to rebuild what included it, or a test
  # goes on passing against the previous build. -j is what makes the compile
  # parallel, and it is most of the saving.
  if ! make -C "$TESTS" -f Makefile ROOT="$ROOT" OUT="$OUT" \
        -j"$(nproc 2>/dev/null || echo 4)" > "$OUT/archive.build.log" 2>&1; then
    # The log is printed as well as named. A build that fails on CI says
    # "BUILD FAIL ... see /tmp/undoStudio-tests/archive.build.log", and that path
    # is on the machine that failed, which is the one place it cannot be read
    # from: the failure arrives with no reason attached to it. The last lines are
    # where a compiler leaves its error.
    echo "  BUILD FAIL  the shared archive (see $OUT/archive.build.log)"
    tail -n 20 "$OUT/archive.build.log" 2>/dev/null | sed 's/^/    /'
    return 1
  fi
  return 0
}

build_and_run() {
  local name="$1" exe="$OUT/$name"
  local -a link=()

  # The shared sources are compiled once, into the archive. A test that needs
  # nothing else is just its own translation unit linked against that.
  local -a src=("$TESTS/$name.cpp")

  if ! [[ " ${PURE[*]} " == *" $name "* ]]; then
    # Both build layouts are searched, since an out-of-source build leaves the
    # libraries under build/ and an in-source one leaves them in the root.
    for l in "${LIBS[@]}"; do
      for dir in "$ROOT" "$ROOT/build"; do
        [ -f "$dir/$l" ] && link+=("$dir/$l")
      done
    done
    # e2e_diagnostics takes an optional file path argument.
    if [ ${#link[@]} -eq 0 ]; then
      echo "  SKIP  $name (build the project first: cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build)"
      return 0
    fi
  fi

  # Both the source root and build/ go into the rpath: the test binaries are run
  # from a temporary directory, so without it the loader cannot find
  # libundoStudioCore.so wherever the build left it.
  if ! g++ -std=c++17 -O1 "${INC[@]}" -o "$exe" "${src[@]}" "$ARCHIVE" \
        "${link[@]}" -Wl,-rpath,"$ROOT" -Wl,-rpath,"$ROOT/build" 2>"$OUT/$name.build.log"; then
    echo "  BUILD FAIL  $name  (see $OUT/$name.build.log)"
    return 1
  fi

  # No test takes an argument any more: they all write the workspace they need.
  # The old "$exe ${1:-}" passed build_and_run's own $1, which is the test name, so
  # e2e_diagnostics was handed a filename of "e2e_diagnostics".
  out=$("$exe" 2>&1)
  rc=$?

  if [[ " ${DIAGNOSTICS[*]} " == *" $name "* ]]; then
    echo "  DIAG  $name (inspection program, not a test)"
    echo "$out" | sed 's/^/          /' | grep -vE "^\s+\[(NAV|undoApp)" | head -12
    return 0
  fi

  # All tests print "RESULT: ..." as their last line.
  if [ $rc -eq 0 ] && echo "$out" | grep -q "RESULT: all checks passed"; then
    echo "  PASS  $name"
    return 0
  fi
  echo "  FAIL  $name"
  echo "$out" | sed 's/^/          /' | grep -vE "^\s+\[(NAV|undoApp)" | head -30
  return 1
}

echo "== building and running the ST editor tests =="
failed=0

# Built once, before any test, so that a failure here is a build failure rather
# than seventeen of them. Skipped when the selection needs no shared code.
selected=()
for t in "${TESTS}"/*.cpp; do
  name=$(basename "$t" .cpp)
  wanted "$name" "$@" && selected+=("$name")
done
if [ ${#selected[@]} -gt 0 ]; then
  if ! build_archive; then
    echo "== SOME TESTS FAILED =="
    exit 1
  fi
fi

for name in "${selected[@]}"; do
  build_and_run "$name" "$@" || failed=1
done

if [ $failed -eq 0 ]; then echo "== all tests passed =="; else echo "== SOME TESTS FAILED =="; fi
exit $failed
