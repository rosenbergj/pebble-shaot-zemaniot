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
src/c/shabbat.c     whether it is Shabbat or yom tov at this moment
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

  **The converse is also a bug.** Anything that sits where Peek never reaches --
  an indicator in a gutter -- must be placed from `layer_get_bounds()`, or it
  moves whenever Peek appears. The disconnect icon was anchored to the visible
  area and rode 29px up the screen every time a timeline card arrived, which
  reads as news about Bluetooth when the news is something else entirely. Ask
  which the element is: content competing for the shrinking area, or an overlay
  in dead space that should hold still.
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
  `emu-touch` and no touch endpoint in the protocol. **The
  accelerometer tap is a different story --
  `pebble emu-tap --emulator emery --direction x+` drives it end to end**,
  and is how the Shabbat suppression and the stale box's inertness were
  checked. Do not read the absence of `emu-touch` as meaning the gesture
  cannot be exercised here; only *touch* cannot.
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
- **`battery_state_service_peek()` is not quantised to 10% on PT2.** That is
  classic-Pebble behaviour; this watch reports values like 32%, so the gauge can
  show what it is given.
- **Judge drawn artwork at 1:1, never zoomed.** Every wrong call in the charging
  icon and disconnect rune came from a 5x crop: vertical stripes look clean
  enlarged and read as a nearly-full battery at true size, and a hand-drawn
  Bluetooth rune's diagonals collapse into an asterisk below ~20px. Screenshot
  candidates in place, at actual size, before choosing.
- **`graphics_draw_rect()` ignores the context's stroke width.** Setting it to
  2 and drawing a rect silently gives a 1px outline; only lines and circles
  honour it. The low-battery cell draws two nested rects instead, so it carries
  the same weight as the 2px disconnect rune facing it. Measure the ink -- the
  difference is invisible at watch size and obvious in a pixel count.
- **`M_PI` is not defined** — the toolchain compiles without GNU extensions.
- **`pebble build` can fail while `pebble install` happily pushes the previous
  `.pbw`.** Never redirect build output to `/dev/null`.
- **A new `messageKeys` entry needs `pebble clean` first.** waf does not treat
  `package.json` as an input to `message_keys.auto.c`, so an incremental build
  fails on `MESSAGE_KEY_<new>` being undeclared while the key sits right there
  in the manifest.
- **Adding a field to `Settings` makes the watch discard its copy — but the
  wearer does not normally see that.** `load_persisted()` compares the stored
  size against `sizeof(Settings)` and falls back to the defaults when they
  differ, which is what stops an old struct being misread through a new layout.
  The phone then heals it: `resendSettings()` pushes Clay's stored values on
  every `ready`, so the real choices are back within a second of launch without
  anyone opening the settings page. Do not tell the wearer their settings will
  be reset; in practice they will not notice.

  It still matters what the defaults are, in the two cases the re-send cannot
  cover: the phone unreachable at launch, and a field so new that Clay has
  never stored a value for it — a genuinely new setting keeps the watch's
  default until the page is saved once, because the store has nothing to send.
  See "Settings, and why the phone re-sends them".
- A wedged emulator needs SIGTERM (never SIGKILL — it corrupts the flash image),
  `rm -f /tmp/pb-emulator.json`, then `pebble wipe`. Beware `pkill -f`, whose
  pattern also matches the shell running it.
- **Every `pebble install --emulator` leaves an emulator behind, and nothing
  reaps it.** The processes reparent to init, so they outlive the session that
  started them, and an idle `qemu-pebble` still burns ~8% of a core — it does not
  idle-halt. `/tmp/pb-emulator.json` names only the most recently launched pair,
  so `pebble kill` stops that one and cannot even see the others. Thirteen had
  accumulated by 2026-08-20, one of them wedged and spinning a full core, ~200%
  CPU between them.

  **`tools/session-cleanup.sh` shuts them down**, along with the screenshot
  gallery server, and the `SessionEnd` hook in `.claude/settings.json` runs it
  automatically when a session ends. It skips `reason: "clear"`, so clearing
  context mid-session leaves a running emulator alone. Run it by hand any time.
  Matching processes is the fiddly part, and the script is written the way it is
  for two reasons worth keeping: `qemu-pebble` is matched with `pkill -x` on the
  process name, and `pypkjs` only after filtering to a real python process --
  plain `pkill -f pypkjs` also matches the shell running it, and kills it
  mid-cleanup.
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
- **...with one exception, and it earns it.** The phone pushes weather, unasked,
  when its JavaScript starts (`ready` in `src/pkjs/index.js`). The pull races
  that startup and loses: the watch asks the instant the Bluetooth link is up,
  but the phone app has not necessarily started the JavaScript that answers,
  and `app_message_outbox_send()` is fire-and-forget with no failed-outbox
  handler. Measured on the emulator, the launch request is lost **every time** —
  the request arrives before the `appmessage` listener is registered, and the
  `ready` push is the only thing that delivers weather at all there. Only the
  phone knows when its JavaScript began running, so this is the one push that
  is better than a pull. It costs a fetch per JS start even where nothing
  displays weather; nothing starts it but the watchface.
- **An unanswered request is chased**, since nothing else can tell. The send is
  fire-and-forget and the phone speaks only when it has something, so a request
  that was never heard looks exactly like one whose answer is still coming; the
  absence of a payload is the only available signal. `weather_retry_ms()` in
  `weather.c` holds the schedule — 10s, 30s, 60s, then stop — and the timer
  lives in `main.c`. Growing gaps because the failures differ in kind: a lost
  race is answered by the first retry, a busy or offline phone wants room. Any
  arriving payload stands the chase down, including an unprompted one. It is an
  `AppTimer` and not a count of ticks **on purpose** — the tick rate is a user
  setting, and a face on minute ticks would round every delay up to a minute.
- **The five-minute sweep is the floor under that**, not the mechanism. It runs
  while the link is up and the box has nothing current — `!have_current ||
  s_wx_stale`. The stale half matters more than the empty half: after a night
  with Bluetooth off the watch *has* weather, just hours-old weather, so a gate
  reading `have_current` alone left the morning to the hourly schedule. It is
  gated on the link because asking across a dead one spends a wake to reach
  nobody, and the connection handler asks the moment the phone is back.
- **Saving settings unchanged does not fetch**, which surprised the wearer once.
  `main.c` drops `settings_changed` when the arriving values memcmp equal to the
  ones held, so the `request_weather()` below it never runs, and Clay re-sends
  identical values on every save. Changing any setting and saving does fetch.
  Left as it is: with the push and the chase in place, a manual force is no
  longer the thing standing between the wearer and current weather.
- **Temperature travels in Celsius** and is converted on the watch, so changing
  the units setting redraws immediately rather than waiting for a fetch.
- **The high and the low can belong to different days.** A daily minimum is a
  pre-dawn reading, so "today's low" is already behind you by mid-morning. From
  06:00 the low is taken from the next day — the one due around dawn tomorrow,
  which is the next one the wearer will actually feel. Between 06:00 and 18:00
  that means the box reads `today` over tomorrow's low, and that is deliberate.
  From 18:00 the box names tomorrow anyway and the two agree again. Both rules
  live in `weather.c` (`weather_wanted_ymd()`, `weather_low_ymd()`) and are
  host-tested at every boundary hour, because the emulator cannot hold a clock
  long enough to check them.
- **The pair is ordered by when the two readings are due, not by size.** Before
  06:00 and after 18:00 it reads low/high, because the next thing coming is the
  low before sunrise with the high ten hours behind it; in between it reads
  high/low. The flip lands on the same two hours as the day rule above, and
  `weather_low_first()` is asserted against `weather_low_ymd()` across all 24 —
  the order and the low's day are the same question. A day shaped oddly, with
  its high at dawn or its low at dusk, will read out of sequence; that is
  accepted rather than modelled.
- **Neither cutoff hour needs tuning, and that is the point of where they sit.**
  A rollover costs the wearer the gap between the value it retires and the
  temperature outside, so the cheapest moment to change what a number means is
  when the two are nearly equal. At 06:00 the overnight low is close to the
  current reading; at 18:00 the day's high is. Anyone dressing to go out reads
  the current temperature first and the high second, so the low changes meaning
  while nobody is looking at it, and the high does the same twelve hours later.
  An hour either way is therefore close to free — do not spend effort refining
  these hours, and do not move them somewhere the gap is wide.
- **The watch picks the forecast day, not the phone.** The box means today
  until 18:00 local and tomorrow after, and that has to roll over on time even
  when no fetch happens. So the phone sends every day it has, each stamped with
  the local date it describes, and `weather_pick_day()` selects. The stamp is
  also what makes going offline across midnight safe: Open-Meteo's day 0
  quietly becomes yesterday, and an unstamped payload would be shown as today's.
- **Three days are carried and only two can ever be shown.** The third is not
  spare capacity; it is how long the box stays right with the phone gone, and
  the two cutoffs are what make the difference so large. A payload fetched on
  day D answers for D and D+1, but from 06:00 on D+1 the low wants D+2 and at
  18:00 on D+1 the box itself rolls to D+2 — so two days ran out around
  lunchtime the day after the last fetch, first substituting a low already
  behind the wearer and then going to `--/--`. Found by wearing it across a
  Shabbat: 26 hours offline and the box had given up by dusk on the second day.
  The third day pushes both boundaries out by 24 hours, which is what it takes
  to cross a Shabbat, a flat phone, or a weekend away and still be right.

  The horizon is pinned down in `test/c` under "how long a payload lasts with
  the phone gone", stated both as invariants over `WEATHER_DAYS` and as four
  dated assertions, so trimming the third day fails there rather than on
  someone's wrist. The emulator cannot help: its clock will not hold still
  long enough to cross even one cutoff.
- **A substituted low is the failure worth knowing about.** When
  `weather_pick_day()` cannot find the low's day, `wx_low_day()` falls back to
  the named day's own low — which by then is a pre-dawn reading already hours
  past, presented in exactly the same shape as a correct one. Only the muted
  staleness ink distinguishes it, and that is a weak signal. It is now confined
  to the last day a payload covers, but it has not been made self-describing.
- **Degrading:** never-fetched shows `--°` centred, with no icon. Data older
  than `WEATHER_STALE_SECS` (3h, six missed refreshes) keeps its place but is
  drawn in a muted ink, icon included — still readable, visibly not live. The
  whole `WeatherData` struct is persisted, so a relaunch is not blank.

  **That fade is a weak signal, and the numbers are on record.** It is a
  contrast reduction and nothing else: white to `#ABABAB`, identical glyphs,
  identical icon, identical position. On the accent fill it drops 6.5:1 to
  2.8:1 and reads as washed out; on the background box it drops 21:1 to
  **8.9:1** -- still nearly twice the threshold for ordinary body text, so it
  simply reads as text. It says nothing about *how* old either: three hours
  and twenty-six hours look identical. Not noticed on the wrist, which
  matches. Replacing it is deferred rather than settled;
  `screenshots/staleness-*.png` are the reference shots.
- **Stale data stops a "Weather now/forecast" box offering "now" at all.** Past
  the threshold it shows the forecast and keeps showing it, with the swapped
  fill, until a fetch lands. A current temperature is the half of that box with
  no shelf life — three hours on it is describing weather the wearer is no
  longer in, and a whole day on it is worthless — while the forecast is still
  answering the question it was asked. The tap goes inert with it, because a box
  already showing the forecast has nothing left to flip to; `tap_has_effect()`
  returns false and the gesture is not offered rather than silently ignored.
- **`wx_swapped()` is why the fill knows about any of this.** A tapped box and a
  stale one are the same statement — this box is not doing its usual thing — so
  they share one predicate and therefore share the inverted fill. They differ
  only in temperament: a tap lasts seconds and reverts itself, staleness lasts
  until the phone comes back. Nothing downstream has to know which is in force.
  The pinned forecast still never swaps, for the reason it never did.
- **`s_wx_stale` is cached in `refresh()`, not asked at draw time.** Three
  decisions read it — what the box contains, which way its fill goes, whether
  the tap does anything — and if they each called `time()` they could disagree
  inside a single frame, drawing a swapped fill around a box that still said
  "now". It is set before every early exit in `refresh()`, because unlike the
  Shabbat predicate it does not need a location to be meaningful.
- **The countdown gets an accent block**, drawn behind the Leco line while it
  is running, with the reading in the on-accent ink. It is sized from the
  measured string but never narrower than `00:00`, so it holds still as the
  minutes drop to one digit and grows rather than clips if a window ever runs
  past an hour. It is centred, then pushed left only as far as the disconnect
  icon's gutter requires — unconditionally, not only while disconnected, since a
  block that slid sideways when Bluetooth dropped would pull the eye away from
  the icon that is the actual news. At 200px wide nothing pushes: centring
  already leaves 5px of gutter.

  Its vertical bounds were set by measuring rendered pixels, not by eye. The
  first attempt put the top border on row 107 with the caption's ink ending at
  106 — touching. Only five rows separate that ink from the Leco digits' ink at
  117, so no shift of the block alone clears the caption without crowding the
  digits; the caption moved up to `COUNTDOWN_LABEL_Y` 92 to make the room. It
  now reads 6px of gap above the block and 7/8 inside it.

- **The forecast has two ways onto the screen.** The "Weather now/forecast"
  slot shows current conditions and swaps on a tap; the "Weather forecast" slot
  is that same forecast rendering pinned open, with no gesture and no revert.
  The pinned one does not swap its fill — the swap means "this box is showing
  the other half of itself just now", which a permanently-configured box never
  is. Two predicates keep this straight and answer different questions:
  `tap_has_effect()` names only the flipping kind, so a face carrying just the
  pinned forecast leaves the gesture inert, while `any_weather_slot()` names
  both and is what gates the fetch.
- **The accelerometer tap** swaps current conditions for the forecast, and back
  after `ALT_VIEW_HOLD_MS` or on a second tap. It sets `s_alt_view`, one flag
  for the whole face rather than one per slot, so anything else that wants a
  tap-driven second face can read the same flag. It reverts on its own because
  screen touch does not reach a watchface (see above) and some accelerometer
  taps are a jostled wrist rather than a decision.
- **The two states are laid out differently, because they have to be.** A
  footer box is 66x57, and the forecast's two numbers will not fit beside the
  icon at a readable size. Eight arrangements were built and screenshotted in
  place; the constraints that killed the rest are recorded where the drawing
  happens, in `draw_face()`. Anything that keeps a header *and* stacks the two
  temperatures needs 62px in a 57px box and will clip.
- **Inverting a box is the face's way of saying "this block is doing something
  unusual"**, not a weather-specific trick. A box normally on the accent fill
  is drawn on the background, and the middle box, normally on the background,
  takes the fill. Reuse it for transient states; do not spend it on anything
  permanent, since its meaning depends on being out of the ordinary.
- **The day word is load-bearing.** The forecast rolls at the cutoff, so a
  layout without it leaves no way to tell whose high is on screen — that is what
  ruled out the arrangements with the largest type. It reads `today` or, past
  the cutoff, the weekday it names (`wed`); "tomorrow" does not fit the label.
- **Icons are Pebble Draw Commands** (`type: "raw"` in `package.json`, 25x25,
  ~1.8KB for all twelve). They carry their own colours, so `wx_recolor()`
  repaints one before it is drawn in a box whose ink differs.

## Shabbat and yom tov

`shabbat.c` answers one question — is it Shabbat or yom tov right now — in four
clauses:

1. any Friday after sundown
2. any Saturday before nightfall
3. any yom tov, by Hebrew date
4. the sundown-to-nightfall window immediately following a yom tov

What it changes so far is the accelerometer tap: with "Suppress taps on Shabbat
and festivals" on, `accel_tap_handler()` returns without toggling, so the
forecast box does not flip. The check lives in the handler and not in
`tap_has_effect()`, which answers a question about how the face is configured
and reads the same all week. The subscription itself is left up rather than
torn down at each boundary — an early return costs less bookkeeping, and
nothing then has to notice the exact second Shabbat ends in order to hand the
gesture back.

- **Clauses 3 and 4 are one idea split in two, and the split is
  `hebdate_for_now()`.** That function rolls the Hebrew date at *sundown*, not
  at midnight — see the `sun_is_up` argument, and note that `refresh()` forces
  the recompute on a bracket flip and not only on a day change. So a yom tov's
  Hebrew date runs sundown to sundown: the right start, because yom tov begins
  at sundown, and the wrong end, because it runs to nightfall. Clause 4 is
  exactly that missing tail, and nothing more.
- **Clause 4 is bound to the sundown-to-nightfall window, not to the countdown
  setting.** They are the same stretch of time, and it is tempting to reuse
  `countdown_active()`. Do not: that is a display toggle, and wiring it in would
  mean switching the countdown off silently shortened Shabbat.
- **Clauses 1 and 4 need an evening guard; clause 2 must not have one.** This is
  the trap in the whole module. "Friday after sundown" read as
  sun-is-down-and-it-is-Friday also matches Friday at four in the morning, and
  the same reading of clause 4 keeps Shabbat running all night and most of the
  next day after a festival ends. Both take `hour >= 12`, the same test
  `hebdate_for_now()` uses for its own rollover, so the two never disagree about
  which Jewish day a dark hour belongs to. Clause 2 takes no such guard, because
  Saturday at four in the morning genuinely is Shabbat. The asymmetry is
  deliberate and both halves of it are host-tested.
- **Thirteen dates, or eight with second days off.** Rosh Hashana is two days
  either way, so this is *not* "drop every second day" and cannot be written as
  one. Two properties of the list do real work: **none of the thirteen falls in
  Adar**, so a leap year adds nothing to reason about and the table is the same
  in all six year lengths; and **none falls on the last day of its Hebrew
  month**, which is what lets clause 4 ask about `heb_day - 1` with no month
  arithmetic and no bounds check — day 0 simply matches nothing.
- **With no location, or where the sun does not set, the answer is
  `SHABBAT_NONE`.** That is a choice of which way to fail, not an oversight: a
  watch that had lost its fix would otherwise behave as though it were Shabbat
  indefinitely, with no way for the wearer to say otherwise. Revisit it once
  something depends on the answer.
- **The definition is the test, not the comment.** `test_shabbat()` walks every
  hour of two civil years — two whole Hebrew years of festivals — in both modes,
  and counts rather than asserting inside the loop, so one mistake does not
  print seventeen thousand times. It checks that the state only ever moves at
  sundown or nightfall, that every Shabbat and every festival is covered to
  nightfall, and that neither trap above has reopened. Removing the evening
  guard, giving clause 2 one, and deleting clause 4 were each confirmed to fail
  it.
- **The emulator can still check the wiring, which the host cannot.** Move the
  location rather than the clock, exactly as for the solar events: with the face
  reporting the clause in a throwaway overlay, sending a longitude far enough
  west puts nightfall back ahead of the current time and the answer flips from
  `none` to `day`. Adding the current Hebrew date to the table for one build
  proves clause 3 is reading `s_heb` and not something else. Both are in
  `screenshots/shabbat-probe-*.png`.

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

## The gutter indicators

Two warnings live in the dead space either side of the centred clock, between
the band and the shaot line: a struck-through Bluetooth rune on the right while
the phone is unreachable, and an empty red battery on the left while the charge
is low. Each has its own toggle, `DisconnectIcon` and `LowBatteryIcon`.

- **They are overlays, and deliberately not part of the five regions.** They are
  drawn from `canvas_update()` after `draw_face()` returns, because
  `draw_face()` gives up early when the unobstructed area is too short for the
  footer, and an indicator that vanishes under a Timeline Peek is not doing its
  job. The gutters are dead space at every time of day, since the clock is
  centred, so nothing has to move to make room.
- **They share one anchor row and never move.** `gutter_top()` measures from
  `layer_get_bounds()`, not from the unobstructed area, so a Timeline Peek does
  not shift them; see the Peek note under "Platform constraints" for what
  anchoring them to the visible area did. Both centre on the same row, and both
  keep a 2px margin from their screen edge, so the pair reads as a matched set
  when they happen to appear together.
- **Only one of the two can be checked properly in the emulator.**
  `pebble emu-battery --percent 12` drives the low-battery mark end to end;
  a genuine disconnect cannot be produced at all (see below). Screenshots of
  the pair are in `screenshots/gutter-*.png`, with the rune force-drawn in the
  two that show both.
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
- **The low-battery cell is drawn empty, and that is the whole point.** A
  proportional fill would be a second, smaller battery gauge arguing with the
  one a footer box may already be showing. This one is not a reading, it is a
  warning; the number is available in a box for anyone who wants it.
- **It uses the footer gauge's threshold and the footer gauge's exception.**
  `GAUGE_LOW_PCT`, and nothing while charging -- a low reading on the charger is
  a state the wearer is already fixing. Two low-battery marks that disagreed
  about what "low" means would be worse than either alone.
- **The terminal nub stays on the right** even though the icon sits opposite the
  rune. Mirroring the placement is the point; mirroring the glyph just stops it
  looking like a battery.

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

**`pebble emu-bt-connection --connected no` kills `pebble logs`.** The log
stream rides the same emulated phone link, so it disconnects along with it and
the tool exits. Bluetooth-drop behaviour therefore cannot be watched live. Test
whatever the drop was going to exercise at app launch instead, which runs the
same code down the same path.

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
**new version**. Bumping it also rewrites `package-lock.json`, which carries
the version in two places — so that file turns up dirty after a build and looks
like churn worth reverting. It is not; reverting it strands the lockfile at an
old version. Commit both. The version is the only thing a phone can see: the filename
never changes, so a build that failed to sync or install is otherwise invisible.
Check the version the phone reports against `BUILD.txt` before concluding
anything about a change.

`--good` copies the staged build over the rollback, tags the commit
`good-<version>`, and rewrites the rollback line in `BUILD.txt` to name the
build it just promoted. The tags are immutable, one per confirmed build, because
after a regression the useful question is what changed since the last good one,
and `git diff good-1.0.4..HEAD` answers it. Nothing is marked good
automatically — only wearing it says that.

The `BUILD.txt` rewrite matters because that file is read on a phone at the
moment a rollback is wanted. It used to be written only by the build path, so
every promotion left it naming the *previous* good build — pointing at a version
the rollback file no longer held. Promoting twice rewrites the line rather than
repeating it.

**`--good` promotes what is in `dist/` *now*, not the build being named.** A
build confirmed after the next one has been staged cannot be promoted with it:
the staged `.pbw` was overwritten in place, and `--good` would tag the wrong
version. Promote it by hand instead — `git tag -a good-<v> <sha>`, then rebuild
that commit in a throwaway `git worktree` and copy the result over the rollback,
checking the bundle's `versionLabel` first (`unzip -p <pbw> appinfo.json`). The
rollback is then a faithful rebuild of the worn commit rather than the bytes
that were worn: same source, version and UUID, not byte-identical.

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
