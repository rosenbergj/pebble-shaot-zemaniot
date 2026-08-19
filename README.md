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
src/c/weather.c     forecast day choice, unit conversion, staleness
resources/fonts/    Liberation Sans Bold, bundled for Hebrew (see below)
resources/data/     weather icons, from TimeStyle (MIT; licence alongside)
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
- **`layer_get_bounds()` is not where content may go.** Timeline Peek covers the
  bottom ~57px, and a face drawn to the full bounds is simply clipped — ours lost
  all three footer boxes. Fill the background over `layer_get_bounds()`, but
  place content inside `layer_get_unobstructed_bounds()`. No subscription is
  needed: the app is redrawn automatically when the area changes. Peek animates
  with a bounce, so intermediate heights either side of the resting value do
  occur; degrade continuously rather than switching between two cases. Test with
  `pebble emu-set-timeline-quick-view on`.
- **Never hold `localtime()`'s pointer across another call to it.** It returns
  the address of one static `struct tm` and refills it every time, so a second
  call rewrites what the first returned. Drawing formats solar times, which
  calls it again — holding the pointer made the civil clock display the sunset
  whenever a solar slot was drawn before it. Copy the struct: `struct tm lt =
  *localtime(&now);`.
- **Cache values, not formatted text.** There is no event for a change to the
  watch's 12/24-hour setting, so anything already rendered to a string stays
  wrong until something else happens to rewrite it. The sunset and tzeit boxes
  had exactly that bug. Keep instants as `time_t` and call `format_hhmm()` at
  draw time; formatting is cheap, and only the solar maths has to stay out of
  the update proc.
- **The large clock faces are numeral subsets.** `FONT_KEY_ROBOTO_BOLD_SUBSET_49`
  and `FONT_KEY_LECO_42_NUMBERS` carry digits and separators, no letters — and
  Roboto has no `.` either, which is why the shaot line is Leco. Anything
  alphabetic on those lines, like `am`/`pm`, has to be set in a second face
  beside them and positioned with `graphics_text_layout_get_content_size()`.
- **The firmware reorders right-to-left text itself.** A Hebrew string drawn
  with `graphics_draw_text` comes out in the correct visual order from
  *logical* byte order -- the month names in `hebdate.c` are stored the way
  they are read, and nothing reverses them. Storing them pre-reversed, or
  reversing at runtime, produces backwards text. Verified on the emulator by
  drawing both orders side by side.
- **No system font carries Hebrew glyphs**, so Hebrew script needs a bundled
  one, and it must cover Latin too: one line can hold `Mon Aug 17 / 5 אלול`,
  and a single `graphics_draw_text` call takes a single font. Liberation Sans
  Bold covers both and is close enough to Gothic to sit beside it; Noto Sans
  Hebrew has **no Latin at all**, which would have forced splitting each line
  into runs and measuring them. `characterRegex` keeps the resource to the two
  blocks actually used. The licence is in `resources/fonts/`.
- **Measure text in a box wider than any line it can produce.** `measure()`
  passes a 1000px-wide rect on purpose: `graphics_text_layout_get_content_size`
  *wraps* the text inside the rect it is given, so measuring inside the real
  200px band returns a width that never exceeds 200 -- and every ladder that
  shrinks text to fit is silently defeated. The band's fallback to 18pt had
  never once fired.
- **A watchface receives no screen touch, and the API says otherwise.** Emery
  declares `PBL_TOUCH` and `touch_service_is_enabled()` returns **true** inside
  a watchface, but neither `touch_service_subscribe()` nor an attached
  `tap_recognizer` ever fires — with the system touch bridge left alone or
  disabled via `window_set_touch_bridge_disabled()`. Confirmed on hardware
  2026-08-18 with a throwaway probe watchface, against an accelerometer-tap
  counter that incremented normally throughout, and with the launch counter
  unmoved, so the taps were not being consumed as system navigation either.
  **Do not feature-test touch with `touch_service_is_enabled()`** — it is the
  check the header recommends and it returns a false positive here. The gate is
  in firmware and invisible to the SDK. Accelerometer tap
  (`accel_tap_service_subscribe()`) is the only input a watchface gets; the
  headers' own advice, in the "User interaction in watchfaces" note, turns out
  to be current after all. None of this is testable in the emulator: there is no
  `emu-touch` and no touch endpoint in the protocol.
- **The emulator's clock resyncs to the host within seconds of being set.**
  `pebble emu-set-time` does take — twice, like `emu-battery` — but it does not
  hold, so anything triggered by the hour cannot be tested by moving the clock
  and then interacting. This is why the forecast's 18:00 cutoff lives in
  `weather.c` as `weather_wanted_ymd()` and is checked on the host instead.
- **`pebble send-app-message` takes several pairs after *one* flag**, as
  `--int 1=42 2=-10`. Repeating the flag (`--int 1=42 --int 2=-10`) silently
  keeps only the last pair, which looks exactly like a watch-side bug. The
  coordinates are the case that catches this: `main.c` adopts a location only
  when both arrive in the same message.
- **`M_PI` is not defined** — the toolchain compiles without GNU extensions.
- **`pebble build` can fail while `pebble install` happily pushes the previous
  `.pbw`.** Never redirect build output to `/dev/null`.
- **A new `messageKeys` entry needs `pebble clean` first.** waf does not treat
  `package.json` as an input to `message_keys.auto.c`, so an incremental build
  fails on `MESSAGE_KEY_<new>` being undeclared while the key sits right there
  in the manifest.
- **Adding a field to `Settings` resets every wearer's settings.**
  `load_persisted()` compares the stored size against `sizeof(Settings)` and
  falls back to the defaults when they differ, which is what stops an old struct
  being misread through a new layout. The price is that any build adding a
  setting starts from defaults until the Clay page is saved again — and the
  defaults are what the wearer gets in the meantime, so choose them as if they
  were the upgrade experience. Say so in the release note; it is invisible
  otherwise.
- A wedged emulator needs SIGTERM (never SIGKILL — it corrupts the flash image),
  `rm -f /tmp/pb-emulator.json`, then `pebble wipe`. Beware `pkill -f`, whose
  pattern also matches the shell running it.
- Installing over a running watchface can leave the old one on screen. If a
  screenshot looks stale, `pebble kill && pebble wipe` and reinstall.

## Weather

The phone fetches from **Open-Meteo** (free, no API key) and sends the result
over AppMessage; the watch never touches the network. Modelled on TimeStyle,
whose weather icons this uses (MIT, licence in `resources/data/`).

- **The watch asks; the phone does not push.** Only the watch knows whether any
  slot is showing weather, and there is no point spending a radio wake and an
  HTTP fetch on a face that is not displaying it. The request is an empty
  message; the phone treats anything from the watch as one. It fires at launch,
  once an hour at a minute derived from the launch time, and on Bluetooth
  reconnect. The per-watch minute is TimeStyle's idea: a fixed `tm_min % 30`
  would have every watch running this face hit the API on the same two ticks.
- **Temperature travels in Celsius** and is converted on the watch, so changing
  the units setting redraws immediately rather than waiting for a fetch.
- **The watch picks the forecast day, not the phone.** The box means today
  until 18:00 local and tomorrow after, and that has to roll over on time even
  when no fetch happens. So the phone sends *both* days, each stamped with the
  local date it describes, and `weather_pick_day()` selects. The stamp is also
  what makes going offline across midnight safe: Open-Meteo's day 0 quietly
  becomes yesterday, and an unstamped payload would be shown as today's.
- **Degrading:** never-fetched shows `--°` centred, with no icon. Data older
  than `WEATHER_STALE_SECS` (3h, six missed refreshes) keeps its place but is
  drawn in a muted ink, icon included — still readable, visibly not live. The
  whole `WeatherData` struct is persisted, so a relaunch is not blank.
- **The accelerometer tap** swaps current conditions for the forecast, and back
  after `ALT_VIEW_HOLD_MS` or on a second tap. It sets `s_alt_view`, one flag
  for the whole face rather than one per slot, so anything else that wants a
  tap-driven second face can read the same flag. It reverts on its own because
  screen touch does not reach a watchface (see above) and some accelerometer
  taps are a jostled wrist rather than a decision.
- **The two states are laid out differently, because they have to be.** A
  footer box is 66x57. Current conditions are one number and sit beside the
  icon on one line under a header. The forecast is two numbers, and in that
  same arrangement the size ladder drops them to 14pt, which is too small to
  read at a glance. So the forecast puts the icon and the day word side by side
  on the top row, freeing the full width beneath for one 24pt line. Stacking
  the two temperatures under a header reads better still and does not fit: a
  header plus two 24pt lines needs 62px. Eight arrangements were built and
  screenshotted in place before this one; anything that keeps the header *and*
  stacks the temperatures will clip.
- **Inverting a box is the face's way of saying "this block is doing something
  unusual"**, not a weather-specific trick. A box normally on the accent fill
  is drawn on the background, and the middle box, normally on the background,
  takes the fill. Reuse it for transient states; do not spend it on anything
  permanent, since its meaning depends on being out of the ordinary.
- **The day word is load-bearing.** The forecast rolls from today to tomorrow
  at the cutoff, so a layout without it leaves no way to tell whose high is on
  screen. That is what ruled out the arrangements with the largest type. It
  reads `today` or, past the cutoff, the weekday it names (`wed`) — the icon
  leaves the label under 40px, where "tomorrow" broke across two lines and
  pushed the temperatures out of the box. A weekday fits in three letters and
  says more than an abbreviation of "tomorrow" would.
- **Icons are Pebble Draw Commands** (`type: "raw"` in `package.json`, 25x25,
  ~1.8KB for all twelve). They carry their own colours, so `wx_recolor()`
  repaints one before it is drawn in a box whose ink differs.

## Settings, and why the phone re-sends them

Settings live in two places: Clay keeps them on the phone, and the watch keeps
its own copy as a single `Settings` struct in persistent storage.

**The watch discards its copy whenever the struct's size changes**, which is
every build that adds a setting — `load_persisted()` compares the stored size
and falls back to the defaults rather than misreading an old layout. The phone
does not know that happened, so it goes on showing the wearer's real choices
while the watch shows defaults, and the two only re-agree when the settings
page is opened and saved. Each half is behaving as written; together they look
broken.

So `src/pkjs/index.js` re-sends whatever Clay has stored on every `ready`. It
is idempotent, costs one message, and heals a reinstall as well as a new field.
Two details it depends on:

- **Only keys this build still declares are sent.** A setting that has since
  been removed — `ClockStyle` was, once — stays behind in Clay's store, and
  would otherwise map to an undefined message key and travel as junk. The
  filter uses `require('message_keys')`, the same mapping Clay itself uses.
- **The watch compares before it writes.** A settings message now arrives on
  every launch, so `inbox_received()` keeps a copy of `Settings` and skips
  `save_settings()` when nothing actually differs. Without that, every launch
  would rewrite flash to no effect.

An empty store is left alone: a watch that has never been configured should
keep its own defaults, and sending an empty dictionary would only churn the
AppMessage buffers.

## The disconnect indicator

A struck-through Bluetooth rune in the right-hand gutter, between the clock and
the shaot line, while the phone is unreachable and `DisconnectIcon` is on.

- **It is an overlay, and deliberately not one of the five regions.** It is
  drawn from `canvas_update()` after `draw_face()` returns, because
  `draw_face()` gives up early when the unobstructed area is too short for the
  footer, and an indicator that vanishes under a Timeline Peek is not doing its
  job. The right gutter is dead space at every time of day, since the clock is
  centred, so nothing has to move to make room.
- **The rune is drawn, not a resource.** It has to read at 25px; TimeStyle's
  disconnect icon is a phone with a cross, which carries more detail than
  survives at that size. Below roughly 20px the rune's diagonals collapse and
  it stops reading at all -- a first attempt at 13px looked like an asterisk.
- **The strike matters.** A plain rune is the symbol for Bluetooth *working*
  almost everywhere else, and although this one only ever appears when the
  phone is gone, a glance should not have to know that.
- **An outline PDC needs fill and stroke set separately.** `pdc_recolor()`
  takes both; `wx_recolor()` passes one colour twice, which is right for the
  weather icons because they are solid shapes and wrong for anything drawn as
  an outline -- doing it to TimeStyle's disconnect icon turned it into a blob.
- **The emulator cannot produce a real disconnect.** pypkjs keeps the phone-app
  connection up, so `pebble emu-bt-connection --connected no` does not reach
  `connection_service_peek_pebble_app_connection()`. Verify by inverting the
  test in a probe build, which proves the icon tracks the flag; the genuine
  disconnected case can only be seen on the watch.

## Driving settings and time in the emulator

The Clay page needs a browser, so it cannot be used from here. Send settings
straight to the app instead:

```sh
pebble send-app-message --emulator emery --int 10011=1   # Countdown on
```

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

**`pebble emu-set-time HH:MM:SS` does not stick.** The emulator's phone bridge
pushes the real time back within a few seconds, so it is good for one screenshot
of a moment and useless for watching anything change across it.

**To watch behaviour change across a solar event, move the location, not the
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

- `pt2-shaot-watchface.pbw` — the latest build. This is the one to install.
- `pt2-shaot-watchface-lastgood.pbw` — the rollback: the most recent build
  confirmed working on the watch. Install this if the latest misbehaves.
- `pt2-shaot-watchface-phase4-js.pbw` — the last JavaScript build (tag
  `phase4-complete`). **Never overwrite it**: `dist/` is gitignored, so git
  cannot restore it, and the tree it came from no longer builds.
- `BUILD.txt` — what is staged: version, commit, and which build the rollback is.

The filenames are stable so that installing never involves a choice about which
file is newest. `tools/deploy.sh` maintains them:

```sh
tools/deploy.sh          # build the current commit into dist/
tools/deploy.sh --good   # mark what is in dist/ as known good
```

A build refuses to stage unless the tree is clean and `package.json` carries a
**new version** (bumping it also rewrites `package-lock.json`, so commit both). That version is the only thing a phone can see: the filename
never changes, so a build that failed to sync or install is otherwise invisible.
Check the version the phone reports against `BUILD.txt` before concluding
anything about a change.

`--good` copies the staged build over the rollback and tags the commit
`good-<version>`. The tags are immutable, one per confirmed build, because after
a regression the useful question is what changed since the last good one, and
`git diff good-1.0.4..HEAD` answers it. Nothing is marked good automatically —
only wearing it says that.

All three builds display as "Shaot Zemaniot" on the watch, and the current two
share a UUID so installing one replaces the other. That is what makes rollback a
single install. Give a build a distinct `displayName` *and* UUID whenever two are
meant to be installed at once for comparison, as `tools/probe*` do.

### History

The face was originally written in JavaScript on Moddable XS (Pebble's "Alloy").
It was finished and correct but could not fit: an Alloy mod lives inside a fixed
**32,768-byte** XS block that cannot be enlarged — `ModdableCreationRecord` can
only re-partition it — and startup commits ~22.5KB, leaving roughly **10KB** for
the whole watchface. The app's ~122KB C heap was never available to JavaScript.

That budget forced the solar maths onto the phone, so the face drifted wrong
after a few days offline. The C rewrite reversed that and uses about 16KB with
~110KB free. The JavaScript sources are on the `phase4-complete` tag.
