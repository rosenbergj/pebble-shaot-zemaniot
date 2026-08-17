# Status

Shaot zemaniot (proportional Jewish hours) watchface for Pebble Time 2 (Emery).
Original brief: `design-idea.txt`.

## Where this stands

**Being rewritten in C on branch `c-port`.** The JavaScript build was finished
and correct but could not fit on the watch: an Alloy mod lives inside a fixed
**32,768-byte** XS block that cannot be enlarged (`ModdableCreationRecord` can
only re-partition it), and ~22.5KB is committed at startup, leaving roughly
**10KB** for the whole watchface. The app's ~122KB C heap was never available to
JavaScript. The C build currently uses ~21.8KB with ~109KB free.

Port progress (branch `c-port`, four commits, working tree clean):

- **Phase A done** — `shaot.c`, `hebdate.c` and `solar.c` ported and verified by
  a host harness (1491 checks) against the same PyEphem/convertdate fixtures the
  JavaScript suite used.
- **Phase B done** — the C watchface renders at pixel-exact parity with the
  JavaScript build; every text row and height matches the reference screenshot.
- **Phase C done** — the phone sends only coordinates, and settings are one
  message key each with Clay handling its own events.
- **Phase D done** — settings and the last known location persist across
  launches; the "waiting for phone" and "no sun window" states both render.
- **Phase E next, and it needs Josh** — sideload and confirm on the real watch.

### Picking this up

    git checkout c-port
    make -C test/c test              # 1491 checks, must stay green
    pebble build && pebble install --emulator emery

A build to sideload is at `dist/pt2-shaot-watchface-c-port.pbw`. It has the
same UUID as the JavaScript build, so **installing it replaces the face
currently on the watch**; `dist/pt2-shaot-watchface-phase4-js.pbw` reinstalls
the old one.

**Two things could not be verified on this headless machine and need checking
on the phone/watch:**

1. **The Clay settings page.** `pebble emu-app-config` needs a browser, so the
   real page never ran. The watch side was proven by sending the same
   dictionary directly from pkjs, and every setting applied; what is untested
   is Clay's own page-to-AppMessage step. Colour is read as either a number or
   a `"0x"` string in case the page sends the latter.
2. **Battery percentage on real hardware** — the emulator always reports 100%.

After Phase E this file has done its job and should be deleted; fold anything
still useful into `README.md` first.

The last known-good JavaScript build is the `phase4-complete` tag (`a7b302f`,
mod 13609), **verified on the real watch** — that is what the tag marks. A built
copy lives at `dist/pt2-shaot-watchface-phase4-js.pbw` and is in daily use;
**do not overwrite it** (`dist/` is gitignored, so git cannot restore it).

Working today: shaot clock with chalakim, Hebrew date with sunset rollover,
civil time with seconds, sunset/nightfall/battery, four configurable display
areas, and on-watch solar maths.

## Layout

- **Watch** — `src/c/main.c` (window, layers, tick, drawing) plus three pure
  modules: `shaot.c` (chalakim), `hebdate.c` (Hebrew calendar) and `solar.c`
  (NOAA sun events). The pure three stay **free of `pebble.h`** on purpose, so
  the host harness can compile and check them with plain gcc.
- **Phone** — `src/pkjs/index.js` and `config.js` (Clay page). Solar maths moved
  back onto the watch; the phone only supplies a location and settings.

## Build and test

    pebble build                     # never suppress the output, see below
    pebble install --emulator emery
    make -C test/c test              # host tests for the pure modules

Test fixtures come from PyEphem and convertdate via `test/gen/*.py`;
`test/gen/json_to_c.py` restates them as C literals for the harness.

## Traps that cost real time

- **`pebble build` can fail while `pebble install` pushes the previous .pbw.**
  Never redirect build output to /dev/null.
- **`graphics_draw_text` adds a font-specific internal leading** that Poco did
  not. `main.c` subtracts a per-font `LEAD_*` constant so the Alloy layout
  coordinates still land correctly.
- **The toolchain links libm without any build configuration**, but compiles
  without the GNU extensions that define `M_PI` — spell the constant out.
- A wedged emulator needs SIGTERM (never SIGKILL, it corrupts the flash image),
  `rm -f /tmp/pb-emulator.json`, then `pebble wipe`. Beware `pkill -f`, whose
  pattern also matches the shell running it.
- Installing over a different running watchface can leave the old one on screen;
  `pebble kill && pebble wipe` before reinstalling if a screenshot looks stale.
- The Pebble Android app's LAN Developer Connection opens no listener, so there
  is no on-device install or logging; verification means sideloading by hand.

The port plan is at `~/.claude/plans/snazzy-squishing-rose.md`; the feature
backlog and the original phase history are in
`~/.claude/plans/new-project-see-the-shimmying-lovelace.md`.
