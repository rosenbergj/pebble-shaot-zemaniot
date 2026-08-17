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
- **Phase E — the C watchface runs on the real watch.** Confirmed 2026-08-17.
  The first sideload gave **"Shaot Zemaniot is not responding"**; the cause was
  **libm's trigonometry**, replaced in `src/c/trig.c`. With that fix the staged
  probe reports `done` and the watchface runs on hardware.

  Still unconfirmed on the watch, and the only things standing between here and
  a finished port: the **Clay settings page** and the **real battery
  percentage**.

### The "not responding" investigation, and how it was found

**Cause: `sin()` overruns the app stack.** newlib's argument reduction,
`__kernel_rem_pio2`, declares three 20-element `double` arrays plus a lookup
table. A Pebble app's stack cannot take it. The emulator tolerates the overrun,
which is why this was invisible until the watchface ran on a watch.

`src/c/trig.c` replaces sin, cos, tan, asin, acos and sqrt with constant-stack
implementations — no arrays, no tables, no recursion. Everything else stays with
libm: plain double arithmetic, `floor()`, `round()` and `fmod()` were each
verified working on hardware.

It matches libm to **1.11e-16** (one ulp) over the ranges the solar code uses,
all 105 zmanim fixtures still pass, the rendered sunset is unchanged, and
dropping newlib's trig cut **7.5KB** from the binary.

**How it was found — a four-build bisect on the watch**, since there are no logs:

| Build | Result | Conclusion |
|---|---|---|
| TimeStyle's own source built here | runs | toolchain fine |
| integer-only probe (`tools/probe-int`) | runs | project setup fine |
| probe using libm (`tools/probe`) | crashes | floating point |
| staged probe (`tools/probe-float`) | reached `sin` | that one call |

The staged probe is the technique worth keeping: it writes each stage number to
**persistent storage before attempting the work**, and runs from a timer after
the first render. A crash cannot unwrite that, so the next launch reports how
far the previous run got. That is the only way to get a reading off this watch.

**Ruled out along the way — do not re-investigate:**

- **Stale persistent storage.** Reproduced deliberately (install the JS build,
  let it write, install C over it without wiping) — came up fine.
- **App packaging.** The header parses clean. `virtual_size` is a `uint16_t` at
  offset **128**, after `resource_crc` and `resource_timestamp`; the wrong
  struct layout makes it look like garbage.
- **SDK version.** Now on **4.33.1**, matching the watch firmware, but that was
  not it: TimeStyle 7.1 declares an *older* `sdk=5.86` than our 5.101 and runs
  daily, and rebuilding changed only the version stamp, CRC, a timestamp and 20
  bytes of metadata — identical machine code, so the syscall ABI never moved.
- **libm coming from the firmware.** It is statically linked in (`T` symbols).
- **Firmware version as such.** The emulator boots the SDK's firmware, so it now
  runs 4.33.1 too and everything passes there. The gap was **real hardware
  versus qemu**.

**A crash inside a layer update proc draws nothing**, because the framebuffer is
only presented once the update returns. "Nothing appeared before the error" does
not mean it died early — that misread cost time here.

### dist/ is a deploy directory, not a build dump

`dist/` is scp'd to a Nextcloud share and installed from a phone, so it holds
**only known-good builds and whatever is currently being tested** — nothing
superseded, nothing known-bad. Every file in it must be distinguishable on a
phone screen: unique UUID *and* unique `displayName`, or two entries in the
watchface picker look identical.

Current contents:

- `pt2-shaot-watchface-phase4-js.pbw` — "Shaot Zemaniot", the known-good daily
  build. **Never overwrite** (`dist/` is gitignored, so git cannot restore it).
- `pt2-shaot-watchface-c-port-v3.pbw` — "Shaot Zemaniot C", with the trig fix.
- `shaot-probe-float.pbw` — "Shaot Probe Float", now exercising `sz_sin` and
  friends, so it verifies the fix rather than re-proving the failure. Launch it
  **twice**: the first run records how far it got, the second reports it.

Settled bisect artifacts were removed once their answer was known. Rebuild any
of them from the repo: `pebble build`, or `tools/probe*/build.sh`. TimeStyle:

    git clone --depth 1 https://github.com/freakified/TimeStylePebble.git
    # set targetPlatforms to ["emery"], change uuid and displayName
    pebble build

The original failing C build was removed from `dist/`: it was known-bad *and*
shared the JS build's UUID, so installing it by mistake would have displaced the
working face. Rebuild it from commit `e25902f` if it is ever needed again.

The `displayName` is "Shaot Zemaniot C" only to tell the builds apart during
testing. **Change it back to "Shaot Zemaniot" once the C build becomes the
daily face.**

**Two things still cannot be verified on this headless machine:**

1. **The Clay settings page.** `pebble emu-app-config` needs a browser, so the
   real page never ran. The watch side was proven by sending the same
   dictionary directly from pkjs, and every setting applied; what is untested
   is Clay's own page-to-AppMessage step. Colour is now read as a number, a
   `"0x"` string, or a byte array.
2. **Battery percentage on real hardware** — the emulator always reports 100%.
   The probe displays it, so it is checkable there.

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
