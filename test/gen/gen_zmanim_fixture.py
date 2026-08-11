#!/usr/bin/env python3
"""Generate zmanim test fixtures using PyEphem as the reference implementation.

Run:  uv run --with ephem python3 test/gen/gen_zmanim_fixture.py > test/fixtures/zmanim.json
"""
import datetime
import json

import ephem

LOCATIONS = [
    ("philadelphia", 39.95, -75.17),
    ("jerusalem", 31.778, 35.235),
    ("helsinki", 60.17, 24.94),
    ("sydney", -33.87, 151.21),
    ("quito", -0.18, -78.47),
]

DATES = [
    (2026, 1, 5),
    (2026, 3, 20),
    (2026, 6, 21),
    (2026, 8, 11),
    (2026, 9, 23),
    (2026, 12, 21),
    (2027, 5, 1),
]

HOURS_UTC = [3, 12, 22]


def to_ms(edate):
    return int(edate.datetime().replace(tzinfo=datetime.timezone.utc).timestamp() * 1000)


def main():
    sun = ephem.Sun()
    entries = []
    for name, lat, lon in LOCATIONS:
        obs = ephem.Observer()
        obs.lat, obs.lon = str(lat), str(lon)
        for y, m, d in DATES:
            for hour in HOURS_UTC:
                t = datetime.datetime(y, m, d, hour, 0, 0)
                obs.date = ephem.Date(t)
                # Default pressure: refraction on; ephem rise/set = upper limb,
                # comparable to NOAA's -0.833 deg center-of-disk convention.
                prev_r = obs.previous_rising(sun)
                prev_s = obs.previous_setting(sun)
                if prev_r > prev_s:
                    prev, nxt, prev_rising = prev_r, obs.next_setting(sun), True
                else:
                    prev, nxt, prev_rising = prev_s, obs.next_rising(sun), False

                # Tzeit: sun's center 8.5 deg below geometric horizon.
                # ephem measures from top of disk, so -8.5 + 0.266 = -8.233;
                # pressure=0 disables refraction.
                obs.pressure = 0
                obs.horizon = "-8.233"
                try:
                    tzeit = to_ms(obs.next_setting(sun))
                except ephem.AlwaysUpError:
                    # White nights: sun never reaches 8.5 deg below horizon
                    tzeit = None
                obs.pressure = 1010
                obs.horizon = "0"

                entries.append(
                    {
                        "name": name,
                        "lat": lat,
                        "lon": lon,
                        "t": int(t.replace(tzinfo=datetime.timezone.utc).timestamp() * 1000),
                        "prev": to_ms(prev),
                        "next": to_ms(nxt),
                        "prevRising": prev_rising,
                        "tzeitNext": tzeit,
                    }
                )
    print(json.dumps(entries, indent=1))


if __name__ == "__main__":
    main()
