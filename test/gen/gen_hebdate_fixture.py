#!/usr/bin/env python3
"""Generate Hebrew-date test fixtures using convertdate as the reference implementation.

Run:  uv run --with convertdate python3 test/gen/gen_hebdate_fixture.py > test/fixtures/hebdate.json
"""
import datetime
import json

from convertdate import hebrew

START = datetime.date(2024, 1, 1)
END = datetime.date(2036, 12, 28)
STEP = datetime.timedelta(days=13)

# Boundary-sensitive dates: Rosh Hashana eves/days, Adar/leap transitions
EXTRAS = [
    (2024, 10, 2), (2024, 10, 3), (2025, 9, 22), (2025, 9, 23),
    (2026, 9, 11), (2026, 9, 12), (2026, 8, 11),
    (2024, 2, 29), (2024, 3, 10), (2027, 3, 9), (2027, 4, 8),
]


def main():
    dates = []
    d = START
    while d <= END:
        dates.append((d.year, d.month, d.day))
        d += STEP
    dates.extend(EXTRAS)

    entries = []
    for y, m, day in dates:
        hy, hm, hd = hebrew.from_gregorian(y, m, day)
        entries.append({"g": [y, m, day], "h": [hy, hm, hd], "leap": hebrew.leap(hy)})

    month_lengths = []
    for hy in (5784, 5785, 5786, 5787, 5790):
        months = hebrew.year_months(hy)
        month_lengths.append(
            {"year": hy, "leap": hebrew.leap(hy),
             "lengths": [hebrew.month_length(hy, mm) for mm in range(1, months + 1)]}
        )

    print(json.dumps({"dates": entries, "monthLengths": month_lengths}, indent=1))


if __name__ == "__main__":
    main()
