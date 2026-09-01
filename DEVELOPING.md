# Working on Shaot Zemaniot

Everything needed to build, run, and release the face. For why it behaves the
way it does, see [DESIGN.md](DESIGN.md).

## Build, test, install

```sh
pebble build                     # never redirect the output (see DESIGN.md)
pebble install --emulator emery
make -C test/c test              # host tests for the pure modules
```

Test fixtures come from PyEphem and convertdate via `test/gen/*.py`;
`test/gen/json_to_c.py` restates them as C literals so the harness needs no JSON
parser. The JSON stays the reviewable ground truth.

There is no on-device install: the Pebble Android app's LAN Developer Connection
opens no listener, so hardware testing means sideloading a `.pbw` by hand.

## Source layout

```
src/c/main.c        window, layers, tick, AppMessage, persistence, drawing
src/c/shaot.c       chalakim arithmetic and formatting
src/c/hebdate.c     Hebrew calendar
src/c/solar.c       NOAA sun events
src/c/trig.c        trigonometry (libm's cannot be used here; see DESIGN.md)
src/c/numparse.c    integer parsing (newlib's strtol cannot be used here)
src/c/weather.c     forecast day choice, unit conversion, staleness
src/c/shabbat.c     whether it is Shabbat or yom tov at this moment
resources/fonts/    Liberation Sans Bold, bundled for Hebrew (see DESIGN.md)
resources/data/     weather icons, and licenses for the health icons
src/pkjs/index.js   phone side: geolocation -> LAT/LON
src/pkjs/config.js  Clay settings page
test/c/             host harness for the pure modules
tools/probe*/       diagnostic watchfaces, see "Debugging on hardware"
```

Everything under `src/c/` **except `main.c` is free of `pebble.h`**, on purpose:
that is what lets the host harness compile and check it with plain gcc against
real fixtures. Keep it that way — it is the only place the math gets tested.

## Driving settings and time in the emulator

The Clay page needs a browser, so it cannot be used from here. Send settings
straight to the app instead:

```sh
pebble send-app-message --emulator emery --int 10011=1   # Countdown on
pebble send-app-message --emulator emery --int 10000=39950000 10001=-75170000
```

**Several keys go in one `--int`, space-separated — repeating the flag silently
keeps only the last pair.** That matters most for `LAT`/`LON`, which
`inbox_received()` adopts only when both arrive in the same message, so sending
them as two flags looks like a message that was ignored rather than a mistake in
the command.

**Seeding Clay's own store** is a different job, and the only way to exercise
the settings re-send without driving the config page by hand. pypkjs keeps
localStorage as a `dbm.dumb` file named for the app UUID:

```sh
~/.local/share/pebble-sdk/<sdk>/emery/localstorage/<uuid>.{dir,dat,bak}
```

Write `clay-settings` into it as a JSON string of flat key/value pairs — select
values as strings, toggles as booleans, exactly as a real save leaves them.
`pebble wipe` clears the watch's own storage but not this, so wiping and then
seeding reproduces the case the re-send exists for: watch on defaults, phone
holding the wearer's real choices.

**The keys must be the numeric ids, not the names** — those are generated into
`build/src/message_keys.auto.c`, so read them from there after a build. This
path also exercises the real `inbox_received` handler, which a hardcoded default
would not.

**`pebble emu-bt-connection --connected no` kills `pebble logs`.** The log
stream rides the same emulated phone link, so it disconnects along with it and
the tool exits. Bluetooth-drop behavior therefore cannot be watched live. Test
whatever the drop was going to exercise at app launch instead, which runs the
same code down the same path.

**`pebble emu-set-time HH:MM:SS` does not stick.** The emulator's phone bridge
pushes the real time back within a few seconds, so it is good for one screenshot
of a moment and useless for watching anything change across it.

**To watch behavior change across a solar event, move the location, not the
clock.** Send a `LAT`/`LON` (scaled by 1e6, as `src/pkjs/index.js` does) chosen
so the event falls near the real current time; the clock then runs normally and
successive screenshots are seconds apart, not resynced out from under you. That
is how the countdown's per-second ticking was verified. Never use a real home
location for this — anywhere with the right sun times will do.

## Debugging on hardware

The watch returns no logs, so `tools/` holds diagnostic watchfaces:

| | |
|---|---|
| `tools/probe-int` | integer-only; proves the project builds into a working app |
| `tools/probe` | adds libm and the solar math |
| `tools/probe-float` | staged: reports exactly which operation failed |
| `tools/probe-loc` | what the location path is doing, and when |
| `tools/touch-probe` | whether a watchface gets screen touch; settled, see its README |

Each has its own UUID so it installs alongside the watchface. `probe`,
`probe-float` and `probe-loc` carry a `build.sh`; `probe-int` and
`touch-probe` are built with `pebble build` in their own directory.

**`probe-float` reaches the shared modules by symlink** — `hebdate`, `shaot`,
`solar` and `trig` under `tools/probe-float/src/c/` are links into `src/c/`, so
the probe always builds the code the face is running rather than a copy that
can drift. Anything that rewrites source files in bulk must skip them or restore
them afterwards: `sed -i` and friends replace a symlink with a regular file, and
the link is silently gone. `probe-loc`'s phone side is the deliberate exception,
a mirror rather than a link, for the reason given below.

`probe-loc` answers a different kind of question from the other three. They ask
whether the app runs at all; it asks **what the phone has told the watch, and
when**. The face shows no coordinates, so a position update is invisible on it
except as a sunset time that moved -- which is the same evidence a new day
produces. The probe shows the coordinates in force, how long ago they arrived,
a persisted log of the last six messages with the distance each one moved, and
how old each fix already was when the phone handed it over. It reports the
distance from the shipping `weather_move_km()` rather than a copy.

That last column is the one to read for tuning. Nothing refreshes position
between the scheduled wakes, so a fix is only ever fresher than the interval
because *another app* on the phone asked for one and the OS handed us theirs --
which is a fact about how the phone is used, not about this code.
`GEO_OPTIONS_CHEAP` allows 30 minutes on that bet, which since the wake went
half-hourly is the interval itself. The probe is how to find out whether the bet pays
on a particular phone rather than guessing.

Its phone side is a deliberate **mirror** of the location half of
`src/pkjs/index.js`, not a symlink -- the real one drags in Clay and the whole
settings surface. What has to stay in step is the schedule: when a fix is taken,
how stale a cached one may be, and what wakes the phone at all. Change that
there and change it here, or the probe stops reporting on the thing it exists
for.

**A watchapp with a phone side needs `"enableMultiJS": true`.** Without it the
build emits `src/pkjs/index.js` into the bundle as `index.js`, which nothing
runs -- the phone app loads `pebble-js-app.js` and finds none. The app installs,
the watch side works, and the phone side is simply never there: no location, no
answer to any request, no error anywhere. Two build warnings say so
(`message_keys.json will not be included`, `pebble-js-app.js does not exist`)
and they are easy to wave off after checking that the JS is in the bundle,
because it is -- under the wrong name. `tools/probe`, `probe-int` and
`probe-float` never caught this: none of them has a phone side. **Check the
bundle for `pebble-js-app.js`, not for a `.js`.**

Two more things it taught while being written. **The log has to be persisted**,
because swapping watchfaces is also how a fresh fix is forced, and the whole
question is what changed when it did. And **`app_message_open()` wants explicit
sizes**: opened with `app_message_inbox_size_maximum()` it took the first
message and silently dropped every one after it, with nothing on screen to say
so. It now opens `(512, 64)` like the face, and registers
`inbox_dropped`/`outbox_failed` handlers that log -- a probe that ignores a
message in silence is worse than no probe.

`probe-float` is the technique worth remembering: **it writes each stage number
to persistent storage before attempting that stage**, and runs the risky work
from a timer after the first render. A crash cannot unwrite persistent storage,
so the next launch reports how far the previous run got. Bump its persist keys
whenever the stage list changes, or an old value reads back against the new
names and lies.

When something works on one build and not another, **bisect with controls** —
build a known-good third-party watchface with this toolchain, and an
integer-only version of your own — rather than reasoning about causes. That
localizes a fault in one round of sideloading instead of several.

For settings problems, read the parsed values out of `pebble logs`, not off the
screen: a screenshot easily catches the frame before the message arrives.

## Producing a `.pbw`

`pebble build` writes `build/pt2-shaot-watchface.pbw`. That is the file to
sideload from the Pebble phone app; there is no on-device install path, as
above.

The version comes from `package.json`, and bumping it also rewrites
`package-lock.json`, which carries the version in two places. That file turns
up dirty after a build and looks like churn worth reverting. It is not —
reverting it strands the lockfile at an old version. Commit both.

Nothing else about releasing lives in this repo. How a given build gets onto a
given wrist is a local matter and differs per person.
