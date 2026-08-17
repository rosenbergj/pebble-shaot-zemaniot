# Status

Shaot zemaniot (proportional Jewish hours) watchface for Pebble Time 2 (Emery).
Original brief: `design-idea.txt`.

## Where this stands

Phases 0–4 are built and feature-complete. The `phase4-complete` tag (`a7b302f`,
mod 13609) is **verified running on the real Pebble Time 2** — that is what the
tag marks. Later commits grew the mod to ~14045 and **fail on the watch** with
`alloy: fatal error, memory full`, so the device limit is bracketed between the
two. The open question is whether to keep trimming JavaScript or rewrite the
watch side in C.

Alloy's budget is a hard **32,768-byte** XS block that cannot be enlarged (the
`ModdableCreationRecord` can only re-partition it), of which ~22.5KB is
committed at startup — leaving roughly **10KB** for the whole watchface. The
app's ~122KB C heap is unavailable to JavaScript. See the plan file for the
source references.

Working today: shaot clock with chalakim, Hebrew date with sunset rollover,
civil time with seconds, sunset/nightfall/battery, four configurable display
areas, a Clay settings page, and phone-supplied sun times.

## Layout

- **Watch** — `src/embeddedjs/main.js` (all watch-side code) and `core.js`
  (shaot arithmetic + Hebrew calendar, no watch APIs so node can test it).
  Only two modules **on purpose**: each module costs XS memory.
- **Phone** — `src/pkjs/solar.js` (NOAA solar maths, CommonJS), `index.js`
  (geolocation, builds a ~4 day window of sun events), `config.js` (Clay page).
- **Two message keys** — `SUN` is a rolling window of `r`/`s`/`t` timestamps so
  the watch stays right for ~3 days offline; `CFG` packs every setting into one
  comma-separated string. One key per setting cost ~1KB and hung the watch.

## Build and test

    pebble build                          # never suppress the output, see below
    pebble install --emulator emery
    node --test 'test/**/*.test.mjs'      # 217 tests

Mod size lives at `build/mods/emery/mcrun/bin/pebble/release/embeddedjs/mc.xsa`.
Test fixtures come from PyEphem and convertdate via `test/gen/*.py`.

## Traps that cost real time

- **The emulator is more permissive than the watch.** Running in QEMU is not
  evidence a build fits on hardware.
- **`pebble build` can fail while `pebble install` pushes the previous .pbw.**
  Never redirect build output to /dev/null.
- Watch-side `console.log` goes nowhere; pkjs logging works. Debug the watch by
  drawing to the screen.
- A wedged emulator needs SIGTERM (never SIGKILL, it corrupts the flash image),
  `rm -f /tmp/pb-emulator.json`, then `pebble wipe`.
- The Pebble Android app's LAN Developer Connection opens no listener, so there
  is no on-device install or logging; verification means sideloading by hand.

Fuller notes, including the memory-ceiling history and what has already been
tried, are in the plan file at
`~/.claude/plans/new-project-see-the-shimmying-lovelace.md`.
