#!/usr/bin/env bash
# Build and run the core tests.
#
# The core does not depend on ImGui, so these need no graphics context at all and
# compile in a second. The Editor's own tests, which do need one, live next to
# their undoApp and are run by its own script.
#
# Usage:  ./run_tests.sh
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
TESTS="$ROOT/src/core/tests"
OUT="${TMPDIR:-/tmp}/undoStudio-core-tests"
mkdir -p "$OUT"

failures=0

for t in "${TESTS}"/*.cpp; do
  name=$(basename "$t" .cpp)

  if ! g++ -std=c++17 -O1 -I "$ROOT/include" \
        -o "$OUT/$name" \
        "$t" "$ROOT/src/core/Settings.cpp" "$ROOT/src/core/ProjectManager.cpp" \
        2>"$OUT/$name.build.log"; then
    echo "  BUILD FAIL  $name  (see $OUT/$name.build.log)"
    failures=$((failures + 1))
    continue
  fi

  # Run each one in a directory of its own: the state files these tests are about
  # are written relative to the working directory, and a test that picked up the
  # developer's own would be testing their session rather than the code.
  work="$OUT/work-$name"
  rm -rf "$work"
  mkdir -p "$work"
  ( cd "$work" && "$OUT/$name" )
  status=$?
  if [ $status -ne 0 ]; then
    echo "  FAIL  $name"
    failures=$((failures + 1))
  else
    echo "  PASS  $name"
  fi
done

if [ $failures -ne 0 ]; then
  echo "== $failures core test(s) failed =="
  exit 1
fi
echo "== all core tests passed =="
