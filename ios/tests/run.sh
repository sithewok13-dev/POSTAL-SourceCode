#!/bin/sh
# Builds and runs the touch controls' core harness on Linux (or macOS).
# Run from anywhere: sh ios/tests/run.sh
set -e
here=$(cd "$(dirname "$0")" && pwd)
out=${TMPDIR:-/tmp}/postal_touch_core_test.$$
${CC:-cc} -std=c99 -Wall -Werror "$here/../IOSTouchCore.c" "$here/touch_core_test.c" -lm -o "$out"
status=0
"$out" || status=$?
rm -f "$out"
exit $status
