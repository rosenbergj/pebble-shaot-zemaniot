# Shaot Zemaniot

A *shaot zemaniot* — proportional Jewish hours — watchface for the Pebble Time 2.

The day is divided into twelve proportional hours between sunrise and sunset,
and twelve more between sunset and sunrise, so a daytime hour is longer in
summer than in winter. The face shows the current proportional hour with
*chalakim* (1/1080th of an hour), the civil time, the Hebrew date, and three
configurable boxes.

The sun times are computed **on the watch**, so the face stays correct offline
indefinitely — the phone only supplies a location.

## What's on the face

- **The shaot line.** The proportional hour, either as hour, minutes, and
  chalakim, or as the raw count of chalakim within the hour. The count can start
  at 0 or at 6, so sunrise reads either 0.00 or 6.00 and true noon either 6.00
  or 12.00. Between sunset and nightfall it can instead count down to nightfall.
- **The civil time**, in a large face or in the same digits as the shaot line.
- **A band across the top** and **three boxes across the bottom**, each of which
  you choose the contents of.
- **Two gutters** either side of the clock, for a struck-through Bluetooth rune
  when the phone is away and a battery glyph when the charge is low.

Any band or box can hold: the Hebrew date, the weekday and secular date, today's
sun data (sunrise, sunset, and nightfall) in various combinations, the battery,
the weather and forecast, or health data. **Tapping the watch** swaps a weather
box to its forecast and moves a "next sun event" box on to the one after it.

Hebrew months can be written in Hebrew or transliterated, temperatures in
Fahrenheit or Celsius, and the accent color is yours to pick. On Shabbat and yom
tov the face can ignore taps, so a wrist movement changes nothing.

## Installing

Grab the `.pbw` from the [releases page][releases] and install it from the
Pebble phone app. Weather and the initial location need the phone; everything
else — the shaot clock, the sun events, the Hebrew date — runs on the watch
alone and keeps working when the phone is away.

Built for the Pebble Time 2 (`emery`) only.

[releases]: https://github.com/rosenbergj/pebble-shaot-zemaniot/releases

## Building it yourself

```sh
pebble build
pebble install --emulator emery
make -C test/c test     # host tests for the calendar and solar math
```

[DEVELOPING.md](DEVELOPING.md) covers the rest: the source layout, driving the
emulator, debugging on hardware, and how releases are cut.

## Patches welcome

Fixes and additions are welcome. Two things worth knowing before you start:

- Everything under `src/c/` **except `main.c` is free of `pebble.h`**, so the
  host test harness can compile and check it with plain gcc against real
  fixtures. Please keep it that way — it is the only place the math gets
  tested. Run `make -C test/c test` before you send a patch.
- [DESIGN.md](DESIGN.md) records why the face behaves as it does, and a fair
  amount of it is hard-won: several Pebble platform behaviors will crash or
  silently corrupt a watchface and are invisible in the emulator. If something
  in the code looks needlessly roundabout, the reason is probably in there.

## License

MIT — see [LICENSE](LICENSE). Bundled third-party assets keep their own terms,
and their full license texts ship alongside them:

| Asset | Source | License |
| --- | --- | --- |
| Weather icons | [TimeStyle](https://github.com/freakified/TimeStylePebble) | MIT — `resources/data/TimeStyle-LICENSE.txt` |
| Hebrew font | [Liberation Sans Bold](https://github.com/liberationfonts) | SIL OFL — `resources/fonts/LiberationSans-LICENSE.txt` |
| Steps icon | [Font Awesome Free](https://fontawesome.com) (`shoe-prints`) | CC BY 4.0 — `resources/data/FontAwesome-LICENSE.txt` |
| Heart icon | [Bootstrap Icons](https://icons.getbootstrap.com) (`heart-fill`) | MIT — `resources/data/BootstrapIcons-LICENSE.txt` |

The two health icons were traced by hand into pixel masks in `src/c/main.c`
rather than bundled as image files; the attribution applies just the same.
