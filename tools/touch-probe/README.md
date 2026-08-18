# Touch probe

A throwaway **watchface** that answers one question the SDK headers cannot:
does a watchface receive screen touch on Emery hardware, and by which API?

Nothing in the public headers gates touch by process type, but nothing grants it
either. The "watchfaces cannot use the buttons" note in `pebble.h` predates touch
hardware and is byte-identical on platforms with no touchscreen at all, so it is
silence rather than a denial. Any real gate lives in firmware. There is no
`emu-touch` in the pebble tool and no touch endpoint in the protocol, so this
cannot be settled in the emulator — only on the watch, by eye.

Separate UUID (`9cca9a76-…`) from Shaot Zemaniot, so installing it adds a second
watchface rather than replacing the first.

## Result — settled 2026-08-18

**A watchface receives no screen touch on Pebble Time 2.** Run on hardware, in
all three modes, only `accel taps` ever moved: `down`, `move`, `up` and
`recog taps` all stayed at 0.

Two readings make it conclusive rather than merely negative:

- **`enabled` read YES** the whole time. `touch_service_is_enabled()` is
  documented as "true if touch input is currently being delivered to apps", so
  in a watchface it reports touch as available and delivers none. It is a false
  positive, and it is the check the header itself recommends for feature
  detection — do not use it.
- **`launches` stayed at 1.** The system was not consuming the taps as
  navigation and bouncing out to the launcher; the events simply go nowhere.

The accelerometer counter incrementing throughout is what rules out "the probe
was not running", which is the whole reason the mode cycle is driven by
accelerometer tap.

So `accel_tap_service_subscribe()` is the only input a watchface gets, and the
`pebble.h` note that watchfaces should use the AccelerometerService — which
reads like a leftover from pre-touch hardware — turns out to still be current.

Kept rather than deleted: if a firmware update ever changes this, re-running the
probe is the cheapest way to find out. It lives here beside `tools/probe`,
`tools/probe-float` and `tools/probe-int`, which answered earlier
hardware-only questions the same way.

## Modes

Three mechanisms could independently be the one that fails. Run together,
silence would not say which was at fault, so the probe runs one at a time:

| Mode     | Mechanism                     | Touch bridge |
| -------- | ----------------------------- | ------------ |
| `RAW`    | `touch_service_subscribe()`   | left alone   |
| `RAW-NB` | `touch_service_subscribe()`   | disabled     |
| `RECOG`  | `tap_recognizer_create()`     | disabled     |

The bridge matters because `window_set_touch_bridge_disabled()` documents that
the system recognizer set otherwise fails on Touchdown for the window — which
leaves open whether it also swallows the raw stream.

**An accelerometer tap cycles the mode.** That is deliberate: the accelerometer
is the one input we are confident a watchface receives, so it doubles as the
control. If the mode never changes, the probe is not running and nothing else on
the screen means anything.

## Reading the screen

- `enabled` — `touch_service_is_enabled()`, re-polled every second.
- `down / move / up` — raw `TouchEvent` counts by type.
- `xy` — last reported coordinate; a red dot is drawn there.
- `nonnav` — the `non_navigational` flag of the last event. `YES` means the
  system considered the interaction session inactive at Touchdown, i.e. the
  touch that wakes an idle watchface. A face that acted on those would flip
  its display every time the wearer woke it.
- `recog taps` — completions from the tap recognizer.
- `accel taps` — accelerometer taps, i.e. mode changes.
- `launches` — increments every time the watchface starts.

Counters persist across relaunch on purpose. If touching the screen bounces you
out to the launcher, the face reloads with everything zeroed, and a zero would
read as "no touch received" when touch was in fact received and acted on by the
system. A jump in `launches` tells those two apart.

## What each outcome means

- **`RAW` counts climb** — watchfaces get raw touch; the richest option, since
  `xy` allows per-box hit testing.
- **Only `RAW-NB` climbs** — the system bridge swallows touch unless disabled.
  Usable, but disabling the bridge may affect system gestures.
- **Only `RECOG` climbs** — use recognizers rather than the raw stream.
- **Nothing climbs in any mode, `accel taps` does** — watchfaces do not get
  touch. The accelerometer is the only route.
- **`nonnav` reads `YES` on every event** — touch arrives but only ever as
  wake-the-screen contact, which is not usable as a deliberate gesture.

## Also worth watching

Whether `accel taps` climbs on its own during ordinary arm movement. Anything
built on "tap to switch what is displayed" inherits that false-positive rate.

## Building

```
pebble build
```

Emulator install (`pebble install --emulator emery`) shows the probe only if it
is the sole watchface installed; otherwise the emulator stays on whatever
watchface was already showing. `pebble kill && pebble wipe` first.

