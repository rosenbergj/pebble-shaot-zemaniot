#!/bin/sh
# Build the location probe.
#
# Shares the watchface's pure modules rather than copying them, so the distance
# it reports is computed by the same code the face decides with. They are
# symlinked in at build time and gitignored; only the probe's own main.c and
# pkjs live here.
set -e
cd "$(dirname "$0")"
for m in solar trig weather; do
  ln -sf "../../../../src/c/$m.c" "src/c/$m.c"
  ln -sf "../../../../src/c/$m.h" "src/c/$m.h"
done
pebble build
echo
echo "built: $(pwd)/build/probe-loc.pbw"
