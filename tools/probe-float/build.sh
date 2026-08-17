#!/bin/sh
# Build the staged floating-point probe. See tools/probe/build.sh for why the
# pure modules are symlinked rather than copied.
set -e
cd "$(dirname "$0")"
for m in shaot hebdate solar; do
  ln -sf "../../../../src/c/$m.c" "src/c/$m.c"
  ln -sf "../../../../src/c/$m.h" "src/c/$m.h"
done
pebble build
echo
echo "built: $(pwd)/build/probe-float.pbw"
