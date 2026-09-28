#!/usr/bin/env bash
# Build and run the headless tests for the ST editor plugin.
#
# The tests need no GPU: they create an ImGui context with a font atlas and
# drive real frames. They reach STApp internals through a `#define private
# public` trick, which is why every test includes the system headers *before*
# that define (see the top of any test file).
#
# Usage:  ./run_tests.sh            build and run everything
#         ./run_tests.sh t decl     run only the tests whose name contains t/decl
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
TESTS="$ROOT/undoApps/undoApp.Editor/tests"
OUT="${TMPDIR:-/tmp}/undoStudio-tests"
mkdir -p "$OUT"

INC=(-I "$ROOT/undoApps/undoApp.Editor/include"
     -I "$ROOT/third_party/st2cpp/include"
     -I "$ROOT/third_party/ImGuiColorTextEdit"
     -I "$ROOT/third_party/imgui"
     -I "$ROOT/include")

ST2CPP_SRC=()
while IFS= read -r f; do ST2CPP_SRC+=("$f"); done < <(
  find "$ROOT/third_party/st2cpp/src" -name '*.cpp' ! -path '*cli*')

# The plugin sources under test.
PLUGIN_SRC=("$ROOT/undoApps/undoApp.Editor/src/undoAppSTSemantic.cpp"
            "$ROOT/undoApps/undoApp.Editor/src/undoAppSTSnippet.cpp"
            "$ROOT/undoApps/undoApp.Editor/src/undoAppST.cpp"
            "$ROOT/third_party/ImGuiColorTextEdit/TextEditor.cpp")

# UI tests additionally need the ImGui/ImGuiManager libraries. Those live in the
# build tree; the plugin links them, so reuse the same artefacts.
LIBS=("$ROOT/libimgui.a" "$ROOT/libimplot.a" "$ROOT/libundoStudioCore.so")

# Tests that only exercise the UI-free semantic layer.
PURE=(semantic_tokens declaration_index member_access)

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

build_and_run() {
  local name="$1" exe="$OUT/$name"
  local -a src=("${PLUGIN_SRC[@]}" "$TESTS/$name.cpp")
  local -a link=()

  if [[ " ${PURE[*]} " == *" $name "* ]]; then
    # No ImGui needed.
    src=("$ROOT/undoApps/undoApp.Editor/src/undoAppSTSemantic.cpp" "$TESTS/$name.cpp")
  else
    for l in "${LIBS[@]}"; do
      [ -f "$l" ] && link+=("$l")
    done
    # e2e_diagnostics takes an optional file path argument.
    if [ -z "$link" ]; then
      echo "  SKIP  $name (build the project first: cmake -S . -B . && make)"
      return 0
    fi
  fi

  if ! g++ -std=c++17 -O1 "${INC[@]}" -o "$exe" "${src[@]}" "${ST2CPP_SRC[@]}" \
        "${link[@]}" -Wl,-rpath,"$ROOT" 2>"$OUT/$name.build.log"; then
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
for t in "${TESTS}"/*.cpp; do
  name=$(basename "$t" .cpp)
  wanted "$name" "$@" || continue
  build_and_run "$name" "$@" || failed=1
done

if [ $failed -eq 0 ]; then echo "== all tests passed =="; else echo "== SOME TESTS FAILED =="; fi
exit $failed
