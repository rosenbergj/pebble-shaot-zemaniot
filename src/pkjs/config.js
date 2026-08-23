// Clay settings page, shown on the phone.
//
// Clay handles these itself and sends one AppMessage key per setting, which
// src/c/main.c reads by key. Note that declaring numeric values below does not
// mean numbers arrive: Clay reads a select's value off a DOM <select>, which is
// always a string, and converts only numbers and booleans. The watch parses
// them; see tuple_to_int() in src/c/main.c.

// Sunset and nightfall show today's, rolling over at local midnight, so they
// are steady all day and can name a time already past. The "Next" kinds instead
// show whichever of their events comes soonest, labelled with its name, so they
// change through the day and after nightfall read tomorrow's.
var SLOT_OPTIONS = [
  { label: "Nothing", value: 0 },
  { label: "Hebrew date", value: 1 },
  { label: "Weekday and date", value: 2 },
  { label: "Sunset", value: 3 },
  { label: "Nightfall (tzeit)", value: 4 },
  { label: "Next sunset or nightfall", value: 7 },
  { label: "Next sunrise or sunset", value: 8 },
  { label: "Next sunrise, sunset, or nightfall", value: 9 },
  { label: "Battery", value: 5 },
  { label: "Weather now/forecast", value: 12 },
  { label: "Weather forecast", value: 13 },
];

// Both dates at once will not fit a footer box, which is a third of the screen
// wide, so these are offered for the band alone. The band shrinks its type a
// size or two when a long Hebrew month makes the line too wide.
var BAND_OPTIONS = SLOT_OPTIONS.concat([
  { label: "Both dates: secular first", value: 10 },
  { label: "Both dates: Hebrew first", value: 11 },
]);

module.exports = [
  {
    type: "heading",
    defaultValue: "Shaot Zemaniot",
  },
  {
    type: "text",
    defaultValue:
      "Proportional Jewish hours. The daylight half of the day is divided " +
      "into twelve hours of 1080 chalakim each, and likewise the night.",
  },
  {
    type: "section",
    items: [
      { type: "heading", defaultValue: "The shaot clock" },
      {
        type: "toggle",
        messageKey: "Offset6",
        label: "Start the count at 6",
        description:
          "On: sunrise is 6.00 and true noon is 12.00. Off: sunrise is 0.00 " +
          "and true noon is 6.00.",
        defaultValue: false,
      },
      {
        type: "toggle",
        messageKey: "WithMinutes",
        label: "Show proportional minutes",
        description:
          "On: hour, then proportional minutes (60 to the hour), then " +
          "chalakim (18 to the minute). Off: the raw count of chalakim " +
          "within the hour, 0 to 1079.",
        defaultValue: true,
      },
      {
        type: "toggle",
        messageKey: "Countdown",
        label: "Count down to nightfall",
        description:
          "Between sunset and nightfall, the shaot line will instead count " +
          "down the minutes and seconds until nightfall. While the " +
          "countdown is on screen, the watch updates every second even if " +
          "\"Update every second\" is off.",
        defaultValue: false,
      },
      {
        type: "select",
        messageKey: "CivilFont",
        label: "Clock face for the civil time",
        options: [
          { label: "Large (Roboto)", value: 0 },
          { label: "Match the shaot digits (Leco)", value: 1 },
        ],
        defaultValue: 0,
      },
    ],
  },
  {
    type: "section",
    items: [
      { type: "heading", defaultValue: "What to show where" },
      {
        type: "text",
        defaultValue:
          "Four areas: the band across the top, then the three boxes along " +
          "the bottom.",
      },
      {
        type: "toggle",
        messageKey: "HebrewScript",
        label: "Hebrew month names in Hebrew",
        defaultValue: false,
        description:
          "Writes the month as \u05D0\u05DC\u05D5\u05DC rather than Elul, " +
          "everywhere the Hebrew date appears. The day stays a numeral.",
      },
      {
        type: "select",
        messageKey: "SlotBand",
        label: "Top band",
        options: BAND_OPTIONS,
        defaultValue: 1,
      },
      {
        type: "select",
        messageKey: "SlotLeft",
        label: "Bottom left",
        options: SLOT_OPTIONS,
        defaultValue: 3,
      },
      {
        type: "select",
        messageKey: "SlotMid",
        label: "Bottom middle",
        options: SLOT_OPTIONS,
        defaultValue: 2,
      },
      {
        type: "select",
        messageKey: "SlotRight",
        label: "Bottom right",
        options: SLOT_OPTIONS,
        defaultValue: 5,
      },
      {
        type: "text",
        defaultValue:
          "\"Weather now/forecast\" shows current conditions. Tap the watch " +
          "to swap it for the forecast for a few seconds. Forecasts show " +
          "current day's high until 6pm and current night's low until 6am.",
      },
      {
        type: "toggle",
        messageKey: "Metric",
        label: "Temperatures in Celsius",
        defaultValue: false,
      },
      {
        type: "toggle",
        messageKey: "DisconnectIcon",
        label: "Mark when the phone is out of range",
        defaultValue: true,
      },
      {
        type: "toggle",
        messageKey: "LowBatteryIcon",
        label: "Mark when the battery is low",
        defaultValue: true,
      },
    ],
  },
  {
    type: "section",
    items: [
      { type: "heading", defaultValue: "Shabbat and festivals" },
      {
        type: "toggle",
        messageKey: "ShabbatSuppressTaps",
        label: "Suppress taps on Shabbat and festivals",
        defaultValue: true,
      },
      {
        type: "toggle",
        messageKey: "SecondDays",
        label: "Second days of festivals",
        defaultValue: true,
      },
    ],
  },
  {
    type: "section",
    items: [
      { type: "heading", defaultValue: "Appearance and battery" },
      {
        type: "color",
        messageKey: "AccentColor",
        label: "Accent colour",
        description: "Used for the top band and the two outer boxes.",
        defaultValue: "0x007882",
        sunlight: true,
      },
      {
        type: "toggle",
        messageKey: "TickSeconds",
        label: "Update every second",
        description:
          "Off updates once a minute instead, which is easier on the " +
          "battery. The chalakim reading then only changes once a minute.",
        defaultValue: true,
      },
    ],
  },
  { type: "submit", defaultValue: "Save" },
];
