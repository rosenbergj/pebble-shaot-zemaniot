# Shaot Zemaniot

A *shaot zemaniot* (proportional Jewish hours) watchface for the Pebble Time 2,
written in C. The original brief is in `design-idea.txt`.

The day is divided into twelve proportional hours between sunrise and sunset,
and twelve more between sunset and sunrise, so an hour is longer in summer than
in winter. The face shows the current proportional hour with *chalakim*
(1/1080th of an hour), the civil time, the Hebrew date, and three configurable
boxes.

The sun times are computed **on the watch**, so the face stays correct offline
indefinitely — the phone only supplies a location.

## Build, test, install

```sh
pebble build                     # never redirect the output; see below
pebble install --emulator emery
make -C test/c test              # host tests for the pure modules
```

Test fixtures come from PyEphem and convertdate via `test/gen/*.py`;
`test/gen/json_to_c.py` restates them as C literals so the harness needs no JSON
parser. The JSON stays the reviewable ground truth.

There is no on-device install: the Pebble Android app's LAN Developer Connection
opens no listener, so hardware testing means sideloading a `.pbw` by hand.

## Layout

```
src/c/main.c        window, layers, tick, AppMessage, persistence, drawing
src/c/shaot.c       chalakim arithmetic and formatting
src/c/hebdate.c     Hebrew calendar
src/c/solar.c       NOAA sun events
src/c/trig.c        trigonometry (libm's cannot be used here -- see below)
src/c/numparse.c    integer parsing (newlib's strtol cannot be used here)
src/pkjs/index.js   phone side: geolocation -> LAT/LON
src/pkjs/config.js  Clay settings page
test/c/             host harness for the pure modules
tools/probe*/       diagnostic watchfaces, see "Debugging on hardware"
```

Everything under `src/c/` **except `main.c` is free of `pebble.h`**, on purpose:
that is what lets the host harness compile and check it with plain gcc against
real fixtures. Keep it that way — it is the only place the maths gets tested.

## Platform constraints

These were each found the hard way, and every one of them was invisible in the
emulator.

**The emulator cannot be trusted for anything stack- or timing-related.** It
boots the firmware bundled with the SDK and tolerates faults that real hardware
does not. A build can pass every emulator check and crash instantly on the
watch.

- **Do not call libm's trigonometry.** newlib's `sin()` reduces its argument
  through `__kernel_rem_pio2`, which declares three 20-element `double` arrays
  plus a lookup table, and that overruns a Pebble app's stack. Use `sz_*` from
  `src/c/trig.h`; they match libm to one ulp and use constant stack.
- **Do not call `strtol()`.** It parsed `"2"` and `"5"` correctly and failed on
  `"1"` in the same build. Use `numparse_int()` from `src/c/numparse.h`.
- More generally, **treat newlib as suspect**. The simple parts are fine —
  `snprintf`, `localtime`, `floor`, `round`, `fmod` and plain `double`
  arithmetic are all exercised constantly and verified on hardware. It is the
  routines with hidden machinery inside them that have failed. Where the grammar
  is small, own it and host-test it.
- **Never do real work in a layer update proc.** It runs deep inside the
  firmware's render path with a render watchdog and a partly-consumed stack.
  `refresh()` runs from the tick handler, the message handler, and once at
  startup; drawing only reads the cache it leaves behind.
- **A crash inside an update proc draws nothing**, because the framebuffer is
  only presented once the update returns. A blank screen says nothing about how
  far the app got.
- **Clay's `select` components send strings.** The component reads a DOM
  `<select>`, whose value is always a string, and `prepareForAppMessage`
  converts only numbers and booleans — declaring numeric options changes
  nothing. Settings that silently do nothing usually mean a value arrived as
  `TUPLE_CSTRING`.
- **AppMessage integers arrive at the narrowest width that fits.** Reading
  `value->int32` from a one-byte tuple takes the following bytes of the
  dictionary as the high end of the number.
- **`graphics_draw_text` adds a font-specific internal leading.** `main.c`
  subtracts a per-font `LEAD_*` constant so the layout coordinates land where
  they are meant to.
- **`M_PI` is not defined** — the toolchain compiles without GNU extensions.
- **`pebble build` can fail while `pebble install` happily pushes the previous
  `.pbw`.** Never redirect build output to `/dev/null`.
- A wedged emulator needs SIGTERM (never SIGKILL — it corrupts the flash image),
  `rm -f /tmp/pb-emulator.json`, then `pebble wipe`. Beware `pkill -f`, whose
  pattern also matches the shell running it.
- Installing over a running watchface can leave the old one on screen. If a
  screenshot looks stale, `pebble kill && pebble wipe` and reinstall.

## Debugging on hardware

The watch returns no logs, so `tools/` holds diagnostic watchfaces:

| | |
|---|---|
| `tools/probe-int` | integer-only; proves the project builds into a working app |
| `tools/probe` | adds libm and the solar maths |
| `tools/probe-float` | staged: reports exactly which operation failed |

Each has `build.sh` and its own UUID so it installs alongside the watchface.

`probe-float` is the technique worth remembering: **it writes each stage number
to persistent storage before attempting that stage**, and runs the risky work
from a timer after the first render. A crash cannot unwrite persistent storage,
so the next launch reports how far the previous run got. Bump its persist keys
whenever the stage list changes, or an old value reads back against the new
names and lies.

When something works on one build and not another, **bisect with controls** —
build a known-good third-party watchface with this toolchain, and an
integer-only version of your own — rather than reasoning about causes. That
localises a fault in one round of sideloading instead of several.

For settings problems, read the parsed values out of `pebble logs`, not off the
screen: a screenshot easily catches the frame before the message arrives.

## Releases

`dist/` is scp'd to a Nextcloud share and installed from a phone, so it holds
**only known-good builds and whatever is currently being tested** — nothing
superseded, nothing known-bad. Every file in it must be distinguishable on a
phone screen: unique UUID *and* unique `displayName`.

- `pt2-shaot-watchface.pbw` — the current C build.
- `pt2-shaot-watchface-phase4-js.pbw` — the last JavaScript build (tag
  `phase4-complete`), kept as a rollback. **Never overwrite it**: `dist/` is
  gitignored, so git cannot restore it.

The two have different UUIDs and *can* be installed side by side, but both
display as "Shaot Zemaniot", so on the watch they would be indistinguishable.
That is deliberate: the C build now carries the real name, and the JavaScript
build is an emergency rollback meant to be installed on its own. The `.pbw`
filenames differ, which is what matters when picking one on the phone. Give any
build a distinct `displayName` again whenever two are meant to be installed at
once for comparison.

### History

The face was originally written in JavaScript on Moddable XS (Pebble's "Alloy").
It was finished and correct but could not fit: an Alloy mod lives inside a fixed
**32,768-byte** XS block that cannot be enlarged — `ModdableCreationRecord` can
only re-partition it — and startup commits ~22.5KB, leaving roughly **10KB** for
the whole watchface. The app's ~122KB C heap was never available to JavaScript.

That budget forced the solar maths onto the phone, so the face drifted wrong
after a few days offline. The C rewrite reversed that and uses about 16KB with
~110KB free. The JavaScript sources are on the `phase4-complete` tag.
