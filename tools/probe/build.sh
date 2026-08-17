#!/bin/sh
# Build the diagnostic probe.
#
# The probe shares the watchface's pure calculation modules rather than copying
# them, so it always exercises the code actually shipping. They are symlinked in
# at build time and gitignored; only the probe's own main.c lives here.
set -e
cd "$(dirname "$0")"
for m in shaot hebdate solar; do
  ln -sf "../../../../src/c/$m.c" "src/c/$m.c"
  ln -sf "../../../../src/c/$m.h" "src/c/$m.h"
done
pebble build
echo
echo "built: $(pwd)/build/probe.pbw"
