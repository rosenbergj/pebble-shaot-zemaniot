"""Grab emery emulator screenshots back to back, for building the store GIF.

    ~/.local/share/uv/tools/pebble-tool/bin/python tools/capture-frames.py OUTDIR SECONDS

Run it with pebble-tool's own interpreter: it drives the tool's screenshot
pipeline directly, color correction included, so frames match
`pebble screenshot`. One process keeps the connection open, which is what makes
it fast -- about 7.5 frames a second, against one every second or two from the
CLI, and fast enough that no displayed second is missed.

Each frame is saved as f<n>_<t0>_<t1>.png with its host capture times. Collapse
consecutive identical frames and one remains per displayed second; the GIF is
then a run of those at 1000 ms each. `pebble publish`'s own GIF capture samples
irregularly and skips seconds, which is why this exists.
"""
import os
import sys
import time

from libpebble2.services.screenshot import Screenshot
from PIL import Image
from pebble_tool.commands.screenshot import ScreenshotCommand

SDK = "4.33.1"

out, duration = sys.argv[1], float(sys.argv[2])
os.makedirs(out, exist_ok=True)
cmd = ScreenshotCommand()
cmd._get_debug_args = lambda: {}
pebble = cmd._connect_emulator("emery", SDK)
start = time.time()
n = 0
while time.time() - start < duration:
    t0 = time.time()
    rows = cmd._correct_colours(Screenshot(pebble).grab_image())
    t1 = time.time()
    w = len(rows[0]) // 3
    Image.frombytes("RGB", (w, len(rows)), b"".join(bytes(r) for r in rows)).save(
        os.path.join(out, "f%04d_%.3f_%.3f.png" % (n, t0, t1)))
    n += 1
print("frames", n, "in", round(time.time() - start, 1), "s")
