// The watchface: layout, slot content, and the tick loop.
//
// Emery is 200x228. Ported from the Alloy/JavaScript build, which could not
// fit on real hardware: an Alloy mod lives inside a fixed 32KB XS block that
// cannot be enlarged, and this feature set does not fit in the ~10KB left
// after startup. C has no such budget, which is also why the solar maths runs
// here again rather than on the phone.
//
// Pure calculation lives in shaot.c, hebdate.c and solar.c, which stay free of
// pebble.h so the host harness in test/c can check them against the same
// fixtures the JavaScript used.

#include <pebble.h>


#include "hebdate.h"
#include "numparse.h"
#include "shabbat.h"
#include "shaot.h"
#include "solar.h"
#include "weather.h"

// --- settings ---------------------------------------------------------------
// Phase B defaults; the phone overwrites these once the Clay page is wired up.

// The single solar kinds name a fact about today and roll over at local
// midnight. The combined kinds answer a different question -- what happens next
// -- so they are not anchored to today at all: after nightfall the next thing
// to happen is tomorrow's sunrise, and saying so beats repeating a sunset that
// is already an hour gone.
typedef enum {
  SLOT_NONE = 0,
  SLOT_HEBREW = 1,
  SLOT_SECDATE = 2,
  SLOT_SUNSET = 3,
  SLOT_TZEIT = 4,
  SLOT_BATTERY = 5,
  // 6 was a sunrise-only box, removed because "today's sunrise" has no obvious
  // rollover hour: midnight leaves it naming a sunrise already hours past, and
  // nothing else is clearly better without a use case to judge against. The
  // number stays retired -- a watch may still have 6 saved, and it should draw
  // an empty box rather than turn into whatever took its place.
  SLOT_NEXT_SET_TZEIT = 7,       // sunset or nightfall
  SLOT_NEXT_RISE_SET = 8,        // sunrise or sunset
  SLOT_NEXT_RISE_SET_TZEIT = 9,  // any of the three
  // Both dates on one line. Offered for the band only: a footer box is a third
  // of the screen and could not hold this at any readable size.
  SLOT_DATES_SEC_HEB = 10,
  SLOT_DATES_HEB_SEC = 11,
  // Current conditions, or the forecast while an accelerometer tap is held
  // open. One kind rather than two: the wearer picks "weather" and taps to see
  // the other half, which is the whole point of the toggle.
  SLOT_WEATHER = 12,
  // The forecast held open: the same rendering, with no gesture and no revert.
  // It does not swap its fill the way a tapped box does -- the swapped fill
  // means "this box is showing the other half of itself just now", which a box
  // configured this way never is.
  SLOT_WEATHER_FC = 13,
} SlotKind;

typedef struct {
  bool offset6;
  bool with_minutes;
  bool tick_seconds;
  bool hebrew_script;
  bool countdown;  // count down to nightfall between sunset and tzeit
  bool metric;     // temperatures in Celsius rather than Fahrenheit
  bool bt_icon;    // show a mark while the phone is unreachable
  bool low_batt_icon;  // show a mark while the battery is at or below the low mark
  bool second_days;    // festivals keep their second day
  bool shabbat_no_taps;  // the tap gesture does nothing on Shabbat or yom tov
  uint8_t slot_band, slot_left, slot_mid, slot_right;
  uint32_t accent;      // 0xRRGGBB
  uint8_t civil_font;   // 0 = Roboto 49, 1 = Leco 42 (matches the shaot face)
} Settings;

static Settings s_settings = {
    .offset6 = false,
    .with_minutes = true,
    .tick_seconds = true,
    .hebrew_script = false,
    .countdown = false,
    .metric = false,
    .bt_icon = true,
    .low_batt_icon = true,
    .second_days = true,
    .shabbat_no_taps = true,
    .slot_band = SLOT_HEBREW,
    .slot_left = SLOT_SUNSET,
    .slot_mid = SLOT_SECDATE,
    .slot_right = SLOT_BATTERY,
    .accent = 0x007882,
    .civil_font = 0,
};

// Supplied by the phone and remembered across launches. Until one arrives the
// face says so rather than inventing a location: sun times for somewhere the
// wearer is not are worse than no sun times at all.
static double s_lat = 0;
static double s_lon = 0;
static bool s_have_location = false;

#define PERSIST_KEY_SETTINGS 1
#define PERSIST_KEY_LAT 2
#define PERSIST_KEY_LON 3
#define PERSIST_KEY_WEATHER 4

// --- weather ----------------------------------------------------------------
//
// The phone fetches; this holds what it sent and decides what to show. The
// decisions that do not need a screen -- which day the forecast box means, how
// old is too old, Celsius to Fahrenheit -- live in weather.c so the host
// harness can check them.

static WeatherData s_wx;

// Icons are Pebble Draw Commands, so they are loaded once and recoloured at
// draw time rather than being reloaded whenever the ink changes.
static GDrawCommandImage *s_wx_icon[WCOND_COUNT];

// The tap-driven alternate view. One flag for the whole face rather than one
// per slot: a tap is a single global gesture, so everything that responds to it
// should change together and revert together. Weather is the only thing reading
// it today -- it swaps current conditions for the forecast -- but anything else
// wanting a second face can read the same flag.
//
// Not persisted: a relaunch should come back in the ordinary view.
static bool s_alt_view = false;
static AppTimer *s_alt_timer = NULL;

// Whether the reading behind "now" is too old to be worth a box. Cached in
// refresh() rather than asked at draw time, because three separate decisions
// read it -- what a weather box contains, which way its fill goes, and whether
// the tap does anything -- and they must not be able to disagree inside one
// frame. It is also the one weather question the drawing used to ask time()
// for itself.
static bool s_wx_stale = false;

// Long enough to read a two-number forecast, short enough that a tap from a
// jostled wrist is not still showing it a minute later. Watchfaces get no
// screen touch, so every tap here is the accelerometer and some of them are
// accidents; see the note in README.md.
#define ALT_VIEW_HOLD_MS 8000

// The minute of the hour at which this watch asks for weather. Randomised at
// startup, as TimeStyle does, so that every watch running this face does not
// hit the API on the same two ticks of the clock.
static uint8_t s_wx_minute;

// Whether the phone is reachable. Peeked at startup and kept current by the
// connection handler, so the overlay does not have to ask on every frame.
static bool s_bt_connected = true;
// Chasing an unanswered request. The watch cannot tell a request the phone
// never heard from one it is still working on -- the send is fire-and-forget
// and the phone replies only when it has something -- so the absence of a
// payload is the only signal there is, and these count how long it has been
// absent. The schedule itself lives in weather.c, where the harness can read
// it. Deliberately an AppTimer rather than a count of ticks: the tick rate is
// a user setting, and a face set to minute ticks would otherwise round every
// delay in that schedule up to the next whole minute.
static AppTimer *s_wx_retry_timer = NULL;
static uint8_t s_wx_attempts = 0;

static void save_weather(void) {
  persist_write_data(PERSIST_KEY_WEATHER, &s_wx, sizeof(s_wx));
}

// --- layout -----------------------------------------------------------------

#define BAND_H 38

// The footer is anchored to the bottom of the *unobstructed* area rather than
// to a fixed y, so Timeline Peek does not simply cover it. On an unobstructed
// 228-high screen this puts the rule back at y=170, the tuned position, and the
// face is pixel-identical to before.
#define FOOTER_ZONE_H 58      // the 1px rule plus 57 of boxes
#define SHAOT_INK_BOTTOM 145  // lowest row the Leco line actually paints
#define FOOTER_MIN_GAP 7      // below that the footer crowds the shaot line

// graphics_draw_text() positions glyphs below the top of its box by a
// font-specific internal leading, which Poco did not add. The y values below
// are the original Alloy coordinates, so each draw subtracts its font's
// leading to land in the same place. Measured from emulator screenshots by
// comparing glyph bounding boxes against the JavaScript build.
#define LEAD_GOTHIC14 2
#define LEAD_GOTHIC24 4
#define LEAD_LECO42 8
#define LEAD_ROBOTO49 9
// Liberation Sans Bold, the bundled Hebrew-capable face. Tuned by eye against
// the same baselines the Gothic sizes sit on.
#define LEAD_HEB24 5
#define LEAD_HEB18 4
#define LEAD_HEB14 3

// Footer box widths when one box has to be wider than a third of the screen.
#define BOX_PAD 8         // breathing room either side of a month name
#define BOX_WIDE_MAX 96   // past this the neighbours get too cramped to read
#define BOX_NARROW_MIN 52 // a neighbour narrower than this cannot hold "12:58"


#define CIVIL_Y 46

// The countdown's caption, in the gap between the civil clock and the Leco
// line. Nothing else is ever drawn there, so it costs no other element room.
#define COUNTDOWN_LABEL_Y 92

// The disconnect indicator's box, in the right-hand gutter. Declared up here
// with the layout constants because the countdown block below has to keep clear
// of it, not only draw_bt_overlay().
#define BT_BOX 25

// The countdown's accent block. It stops short of the screen edges on both
// sides: the reading is only ever a few glyphs wide, and the right-hand gutter
// belongs to the disconnect icon, which must stay legible while the countdown
// is running -- that is exactly when a wearer wants to know the phone is gone.
#define COUNTDOWN_BOX_TOP 110
#define COUNTDOWN_BOX_BOTTOM 154
#define COUNTDOWN_BOX_PAD 10  // either side of the widest reading
#define COUNTDOWN_BT_GAP 4    // clear space left of the disconnect icon

// The am/pm marker, shown only when ticking once a minute on a 12-hour clock.
#define MERIDIEM_GAP 5
#define MERIDIEM_NUDGE 3  // lifts it off the very bottom of the text box

// Title case, not caps: these sit beside "sunset", "tzeit" and "batt", which
// are lowercase words, so a shouting weekday was the only one out of step.
static const char *const WDAYS[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
static const char *const GMONTHS[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                      "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

static Window *s_window;
static Layer *s_canvas;

static GFont s_font_shaot;    // Leco 42
static GFont s_font_bold24;   // Gothic 24 Bold
static GFont s_font_bold18;   // Gothic 18 Bold, for a band line that will not fit
static GFont s_font_bold14;   // Gothic 14 Bold, likewise
static GFont s_font_label;    // Gothic 14
static GFont s_font_civil;    // Roboto 49 or the shaot face
// Liberation Sans Bold, bundled. Covers Latin and Hebrew in one face, which is
// what lets a mixed line be drawn in a single call -- and the firmware reorders
// the Hebrew run itself, so the strings stay in logical order.
static GFont s_font_heb24;
static GFont s_font_heb18;
static GFont s_font_heb14;
static int s_lead_civil;      // leading of whichever civil face is selected

static GColor s_bg, s_fg, s_dim, s_accent, s_on_accent, s_rule;

// --- cached state -----------------------------------------------------------

static SolarBracket s_br;
static HebrewDate s_heb;
// The solar events are cached as instants, not as text. Formatting happens at
// draw time, so a change to the watch's 12/24-hour setting takes effect on the
// very next frame. Cached strings did not: they were rewritten only on a day
// change or a bracket flip, and Pebble raises no event when the time format
// changes, so nothing could invalidate them.
static time_t s_sunset_at;
static time_t s_tzeit_at;
static bool s_have_sunset;
static bool s_have_tzeit;

// The next occurrence of each event, for the combined slots. Separate from the
// three above, which are today's whether or not they have happened yet.
// Recomputed once the earliest of them has passed, and only while a slot that
// needs them is actually configured.
static time_t s_next_rise, s_next_set, s_next_tz;
static bool s_have_next_rise, s_have_next_set, s_have_next_tz;
static time_t s_next_stale;
static time_t s_next_from;  // the "now" it was computed from, so a backward
                            // jump in the clock invalidates it too
static int s_last_day = -1;
// Shabbat or yom tov, recomputed every tick. Nothing reads it yet -- what the
// face does differently is a separate decision -- but the inputs it needs are
// only assembled here, so it is computed where they live rather than left to a
// future caller to rediscover.
static ShabbatKind s_shabbat = SHABBAT_NONE;
static int s_battery = 0;
static bool s_charging = false;
// Whichever unit is currently subscribed. Zero is no unit, so the first call
// always subscribes; after that subscribe_tick() is a no-op unless the rate
// really has to change.
static TimeUnits s_tick_unit = 0;

static void subscribe_tick(void);
static void request_weather(void);

static void apply_settings(void) {
  uint8_t r = (s_settings.accent >> 16) & 0xFF;
  uint8_t g = (s_settings.accent >> 8) & 0xFF;
  uint8_t b = s_settings.accent & 0xFF;
  s_accent = GColorFromRGB(r, g, b);
  // Ink on the accent has to follow the accent: white on a pale colour is
  // unreadable. Perceived brightness (ITU-R BT.601) picks the dark or light
  // ink we already have, so any colour in the picker stays legible.
  s_on_accent = ((r * 299 + g * 587 + b * 114) / 1000 > 140) ? s_bg : s_fg;
  if (s_settings.civil_font) {
    s_font_civil = s_font_shaot;
    s_lead_civil = LEAD_LECO42;
  } else {
    s_font_civil = fonts_get_system_font(FONT_KEY_ROBOTO_BOLD_SUBSET_49);
    s_lead_civil = LEAD_ROBOTO49;
  }
}

// --- persistence ------------------------------------------------------------

// A latitude/longitude pair we are willing to compute from. Anything else is
// corrupt: a bad value must leave the face saying so rather than propagate
// through the solar maths into a wild time_t.
static bool coords_sane(double lat, double lon) {
  // NaN fails every comparison, so this rejects it too.
  return (lat >= -90.0 && lat <= 90.0) && (lon >= -180.0 && lon <= 180.0);
}

// The settings struct is written whole, so a build that changes its layout
// would misread an old one. Comparing the stored size catches that and falls
// back to the defaults rather than showing garbage.
static void load_persisted(void) {
  if (persist_exists(PERSIST_KEY_SETTINGS) &&
      persist_get_size(PERSIST_KEY_SETTINGS) == (int)sizeof(Settings)) {
    persist_read_data(PERSIST_KEY_SETTINGS, &s_settings, sizeof(Settings));
  }
  // Range-check rather than trust: persistent storage is keyed by app UUID and
  // survives reinstalls, so these ints can predate this build entirely.
  // Weather survives a relaunch so the face is not blank for the first half
  // hour after every reboot. Same size check as the settings, for the same
  // reason: the struct is written whole.
  if (persist_exists(PERSIST_KEY_WEATHER) &&
      persist_get_size(PERSIST_KEY_WEATHER) == (int)sizeof(WeatherData)) {
    persist_read_data(PERSIST_KEY_WEATHER, &s_wx, sizeof(s_wx));
  }
  if (persist_exists(PERSIST_KEY_LAT) && persist_exists(PERSIST_KEY_LON)) {
    double lat = persist_read_int(PERSIST_KEY_LAT) / 1000000.0;
    double lon = persist_read_int(PERSIST_KEY_LON) / 1000000.0;
    if (coords_sane(lat, lon)) {
      s_lat = lat;
      s_lon = lon;
      s_have_location = true;
    }
  }
}

static void save_settings(void) {
  persist_write_data(PERSIST_KEY_SETTINGS, &s_settings, sizeof(Settings));
}

static void save_location(void) {
  persist_write_int(PERSIST_KEY_LAT, (int32_t)(s_lat * 1000000.0));
  persist_write_int(PERSIST_KEY_LON, (int32_t)(s_lon * 1000000.0));
}

// --- helpers ----------------------------------------------------------------

// Whether to show a 24-hour clock. This follows the watch's own setting and is
// deliberately not a setting of our own: it is a system-wide preference, and a
// watchface that disagreed with the rest of the watch would just be wrong. The
// face used to hardcode 12-hour and ignore it entirely.
static bool use_24h(void) {
  return clock_is_24h_style();
}

// A time of day, in whichever convention is in force. The sunset and tzeit
// boxes go through here too, so they follow the main clock.
//
// localtime() returns NULL for a time_t it cannot break down, so a bad solar
// result must not be dereferenced -- that is a hard fault on the watch, where
// there is no console to see it happen.
static void format_hhmm(time_t t, char *out, size_t n) {
  struct tm *lt = localtime(&t);
  if (!lt) {
    snprintf(out, n, "--:--");
    return;
  }
  if (use_24h()) {
    // Padded, as a 24-hour clock conventionally is: 09:53, not 9:53.
    snprintf(out, n, "%02d:%02d", lt->tm_hour, lt->tm_min);
    return;
  }
  int h = lt->tm_hour % 12;
  if (h == 0) h = 12;
  snprintf(out, n, "%d:%02d", h, lt->tm_min);
}

static const char *ordinal_suffix(int n) {
  int tens = n % 100;
  if (tens >= 11 && tens <= 13) return "th";
  switch (n % 10) {
    case 1: return "st";
    case 2: return "nd";
    case 3: return "rd";
    default: return "th";
  }
}

// Whichever of the requested events comes soonest, labelled with its own name
// so the slot says what it is showing. The label is the point: a bare time that
// silently changes meaning at sunset would be worse than no slot at all.
static void next_event_content(bool want_rise, bool want_set, bool want_tz,
                               char *label, size_t label_n, char *value, size_t value_n) {
  const char *name = NULL;
  time_t best = 0;

  if (want_rise && s_have_next_rise) {
    name = "sunrise";
    best = s_next_rise;
  }
  if (want_set && s_have_next_set && (!name || s_next_set < best)) {
    name = "sunset";
    best = s_next_set;
  }
  if (want_tz && s_have_next_tz && (!name || s_next_tz < best)) {
    name = "tzeit";
    best = s_next_tz;
  }

  if (!name) {
    snprintf(label, label_n, "next");
    snprintf(value, value_n, "--:--");
    return;
  }
  snprintf(label, label_n, "%s", name);
  format_hhmm(best, value, value_n);
}

// How a footer box arranges the two rows it is given. The band ignores this: it
// is one line of text and reads `label`/`value` directly.
typedef enum {
  SLOT_LAYOUT_LABEL,  // small label above a large value -- the common case
  SLOT_LAYOUT_SPLIT,  // one thing broken over both rows, same size, no label
  SLOT_LAYOUT_GAUGE,   // a drawn gauge in place of the label, value below
  SLOT_LAYOUT_WEATHER, // label, then an icon and a temperature side by side
} SlotLayout;

// Which day the forecast half means right now. The rule itself lives in
// weather.c so the host harness can check it at hours the emulator will not
// hold still for.
static int32_t wx_wanted_ymd(const struct tm *lt) {
  return weather_wanted_ymd(lt->tm_year + 1900, lt->tm_mon + 1, lt->tm_mday, lt->tm_hour);
}

// The low can come from a different day than the high; see weather_low_ymd().
// Falls back to the named day when we do not hold the other one, which happens
// only to a payload fetched before local midnight, and that reading is already
// old enough to be drawn muted.
static int wx_low_day(const struct tm *lt, int named_day) {
  const int32_t ymd = weather_low_ymd(lt->tm_year + 1900, lt->tm_mon + 1, lt->tm_mday, lt->tm_hour);
  const int day = weather_pick_day(&s_wx, ymd);
  return (day >= 0) ? day : named_day;
}

static int wx_display_temp(int celsius) {
  return s_settings.metric ? celsius : weather_c_to_f(celsius);
}

// Whether a "Weather now/forecast" box is currently showing its other half,
// for either of the two reasons it can be: a tap is holding it open, or the
// reading behind "now" has gone stale and it has stopped offering "now" at all.
//
// Both are "this box is not doing its usual thing", which is why they share a
// predicate and therefore share the swapped fill. They differ in temperament:
// a tap is a few seconds and reverts itself, staleness lasts until a fetch
// lands. Nothing downstream has to care which is in force.
static bool wx_swapped(void) {
  return s_alt_view || s_wx_stale;
}

// Whether a slot of this kind is showing the forecast at this moment: the
// pinned kind always, the flipping kind while it is swapped.
static bool kind_shows_forecast(uint8_t kind) {
  return kind == SLOT_WEATHER_FC || (kind == SLOT_WEATHER && wx_swapped());
}

// Whether anything on the face needs a weather payload at all. This gates the
// fetch, and is deliberately not the same question as whether a tap does
// something: a face whose only weather box is the pinned forecast wants the
// data every hour and wants nothing to do with the gesture.
static bool any_weather_slot(void) {
  const uint8_t k[4] = {s_settings.slot_band, s_settings.slot_left, s_settings.slot_mid,
                        s_settings.slot_right};
  for (int i = 0; i < 4; i++) {
    if (k[i] == SLOT_WEATHER || k[i] == SLOT_WEATHER_FC) return true;
  }
  return false;
}

// Whether a tap would visibly change anything. A gesture that silently does
// nothing is worse than no gesture, so the handler checks before toggling.
//
// **This is the one to widen** as more things learn to read s_alt_view -- the
// suspend-the-countdown idea in the plan file would add a term here, true while
// the countdown is running, so a tap reaches it on a face carrying no weather
// at all. Widen this and not any_weather_slot(), which asks a different
// question: adding the countdown there would spend a radio wake every hour
// fetching weather that nothing on the face can display.
//
// The pinned forecast is deliberately absent from the terms below. It reads
// the weather but looks identical before and after a tap, so a face showing
// only that should leave the gesture inert.
//
// Stale data makes the flipping kind inert for the same reason: it is already
// showing the forecast and has nothing left to flip to, since the whole point
// of the switch is that "now" is no longer worth offering.
//
// Note the shape. s_wx_stale narrows the *weather* term and is deliberately not
// an early return over the whole function: a second term added here -- the
// suspend-the-countdown idea is the candidate -- must not be switched off
// because the weather happens to be old. Add such a term as another `||`
// alongside weather_flips, never above the staleness check.
static bool tap_has_effect(void) {
  const bool weather_flips =
      !s_wx_stale &&
      (s_settings.slot_left == SLOT_WEATHER || s_settings.slot_mid == SLOT_WEATHER ||
       s_settings.slot_right == SLOT_WEATHER || s_settings.slot_band == SLOT_WEATHER);
  return weather_flips;
}

// Whether this kind's value row can contain a Hebrew month name.
static bool slot_has_hebrew(uint8_t kind) {
  return kind == SLOT_HEBREW || kind == SLOT_DATES_SEC_HEB || kind == SLOT_DATES_HEB_SEC;
}

// Fills label and value, and says how the box should lay them out.
static SlotLayout slot_content(uint8_t kind, const struct tm *lt, bool for_band,
                               char *label, size_t label_n, char *value, size_t value_n) {
  label[0] = '\0';
  value[0] = '\0';

  switch (kind) {
    case SLOT_HEBREW: {
      const char *month = hebdate_month_name(s_heb.year, s_heb.month, s_settings.hebrew_script);
      if (for_band) {
        snprintf(value, value_n, "%d %s", s_heb.day, month);
        return SLOT_LAYOUT_LABEL;
      }
      // In a box, split across both lines ("29th of" / "Av") rather than
      // spending a line on a label: the widest thing is then just the month
      // name, so long ones like Heshvan still fit.
      snprintf(label, label_n, "%d%s of", s_heb.day, ordinal_suffix(s_heb.day));
      snprintf(value, value_n, "%s", month);
      return SLOT_LAYOUT_SPLIT;
    }
    case SLOT_SECDATE:
      if (for_band) {
        snprintf(value, value_n, "%s %s %d", WDAYS[lt->tm_wday], GMONTHS[lt->tm_mon], lt->tm_mday);
        return SLOT_LAYOUT_LABEL;
      }
      snprintf(label, label_n, "%s", WDAYS[lt->tm_wday]);
      snprintf(value, value_n, "%s %d", GMONTHS[lt->tm_mon], lt->tm_mday);
      return SLOT_LAYOUT_LABEL;
    // Both dates, weekday first in either order. The whole line goes in value
    // with no label, so the band prints it bare like the single dates do.
    case SLOT_DATES_SEC_HEB:
      snprintf(value, value_n, "%s %s %d / %d %s", WDAYS[lt->tm_wday], GMONTHS[lt->tm_mon],
               lt->tm_mday, s_heb.day,
               hebdate_month_name(s_heb.year, s_heb.month, s_settings.hebrew_script));
      return SLOT_LAYOUT_LABEL;
    case SLOT_DATES_HEB_SEC:
      snprintf(value, value_n, "%s %d %s / %s %d", WDAYS[lt->tm_wday], s_heb.day,
               hebdate_month_name(s_heb.year, s_heb.month, s_settings.hebrew_script),
               GMONTHS[lt->tm_mon], lt->tm_mday);
      return SLOT_LAYOUT_LABEL;
    case SLOT_SUNSET:
      snprintf(label, label_n, "sunset");
      if (s_have_sunset) format_hhmm(s_sunset_at, value, value_n);
      else snprintf(value, value_n, "--:--");
      return SLOT_LAYOUT_LABEL;
    case SLOT_TZEIT:
      snprintf(label, label_n, "tzeit");
      if (s_have_tzeit) format_hhmm(s_tzeit_at, value, value_n);
      else snprintf(value, value_n, "--:--");
      return SLOT_LAYOUT_LABEL;
    case SLOT_NEXT_SET_TZEIT:
      next_event_content(false, true, true, label, label_n, value, value_n);
      return SLOT_LAYOUT_LABEL;
    case SLOT_NEXT_RISE_SET:
      next_event_content(true, true, false, label, label_n, value, value_n);
      return SLOT_LAYOUT_LABEL;
    case SLOT_NEXT_RISE_SET_TZEIT:
      next_event_content(true, true, true, label, label_n, value, value_n);
      return SLOT_LAYOUT_LABEL;
    case SLOT_WEATHER:
    case SLOT_WEATHER_FC: {
      const bool fc = kind_shows_forecast(kind);
      const int32_t wanted = wx_wanted_ymd(lt);
      const int day = fc ? weather_pick_day(&s_wx, wanted) : -1;
      if (fc) {
        // Naming the day rather than saying "forecast" is the whole label-side
        // cue: it is what tells the wearer which day they are looking at, and
        // it changes at the cutoff without a tap.
        //
        // Tomorrow is named, not called "tomorrow": the icon takes 25 of the
        // box's 67 pixels, and the eight letters broke across two lines and
        // pushed the temperatures out of the box. A weekday fits in three and
        // says more. The wanted day is only ever today or the next, so the
        // name is always one step round from today's.
        static const char *kWday[7] = {"sun", "mon", "tue", "wed", "thu", "fri", "sat"};
        snprintf(label, label_n, "%s",
                 wanted == weather_ymd(lt->tm_year + 1900, lt->tm_mon + 1, lt->tm_mday)
                     ? "today"
                     : kWday[(lt->tm_wday + 1) % 7]);
        if (day >= 0) {
          // Ordered by when they are due, not by which is larger.
          const int hi = wx_display_temp(s_wx.day_high_c[day]);
          const int lo = wx_display_temp(s_wx.day_low_c[wx_low_day(lt, day)]);
          const bool low_first = weather_low_first(lt->tm_hour);
          snprintf(value, value_n, "%d/%d", low_first ? lo : hi, low_first ? hi : lo);
        } else {
          snprintf(value, value_n, "--/--");
        }
      } else {
        snprintf(label, label_n, "now");
        if (s_wx.have_current) snprintf(value, value_n, "%d\u00b0", wx_display_temp(s_wx.temp_c));
        else snprintf(value, value_n, "--\u00b0");
      }
      // The band is one line of text with no room for an icon beside it, so
      // there weather reads "now: 72" and only a box gets the picture.
      return for_band ? SLOT_LAYOUT_LABEL : SLOT_LAYOUT_WEATHER;
    }
    case SLOT_BATTERY:
      snprintf(label, label_n, "batt");
      // The reading drifts while the charger is attached, so say what is
      // happening instead of quoting a number that is about to be wrong.
      if (s_charging) snprintf(value, value_n, "chg");
      else snprintf(value, value_n, "%d%%", s_battery);
      // The band is one line of text and cannot hold a drawn gauge, so there it
      // stays "batt: 78%"; only a box gets the meter.
      return for_band ? SLOT_LAYOUT_LABEL : SLOT_LAYOUT_GAUGE;
    default:
      return SLOT_LAYOUT_LABEL;
  }
}

// The band is a single line, so a labelled slot joins its two parts with a
// colon -- "sunset: 7:58" -- where a footer box stacks them. Content that names
// itself, meaning either date, carries no label and is shown as it stands.
static void band_content(uint8_t kind, const struct tm *lt, char *out, size_t out_n) {
  char label[24], value[40];
  slot_content(kind, lt, true, label, sizeof(label), value, sizeof(value));
  if (label[0]) {
    snprintf(out, out_n, "%s: %s", label, value);
  } else {
    snprintf(out, out_n, "%s", value);
  }
}

// y is the intended top of the glyphs; lead is the font's internal leading.
static void draw_centered(GContext *ctx, const char *text, GFont font, int lead,
                          GColor color, int y, int x, int w) {
  graphics_context_set_text_color(ctx, color);
  graphics_draw_text(ctx, text, font, GRect(x, y - lead, w, 60),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
}

// As above, but starting at x rather than centred in a box. Used where two runs
// of text in different faces have to sit next to each other on one line.
static void draw_at(GContext *ctx, const char *text, GFont font, int lead,
                    GColor color, int y, int x, int w) {
  graphics_context_set_text_color(ctx, color);
  graphics_draw_text(ctx, text, font, GRect(x, y - lead, w, 60),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
}

// A battery meter, drawn in a box's label row in place of the word "batt": an
// outlined cell with a terminal nub, filled left to right in proportion to the
// charge. It takes the same ink as the value below it, so it follows the
// accent-contrast choice without knowing anything about the palette.
#define GAUGE_W 26      // the body, excluding the nub
#define GAUGE_H 12
#define GAUGE_NUB_W 2
#define GAUGE_NUB_H 6
#define GAUGE_LOW_PCT 20  // at or below this the meter goes red, as TimeStyle does

// The mark drawn inside the cell while charging: a plug, its lead entering from
// the left and its prongs pointing at the cell's terminal nub, so the shape
// reads as something being plugged in rather than pulled out. Built from rects
// rather than a Pebble Draw Command resource -- the interior is 22x8, which is
// too small to be worth a resource, and stripes or a bolt both read as a charge
// level rather than as the absence of one.
static void draw_charge_mark(GContext *ctx, int x0, int y0, GColor ink) {
  const int w = GAUGE_W - 4;
  const int h = GAUGE_H - 4;
  graphics_context_set_fill_color(ctx, ink);
  graphics_fill_rect(ctx, GRect(x0, y0 + 3, w - 14, 2), 0, GCornerNone);       // lead
  graphics_fill_rect(ctx, GRect(x0 + w - 14, y0, 9, h), 0, GCornerNone);       // body
  graphics_fill_rect(ctx, GRect(x0 + w - 5, y0 + 1, 5, 2), 0, GCornerNone);    // prongs
  graphics_fill_rect(ctx, GRect(x0 + w - 5, y0 + 5, 5, 2), 0, GCornerNone);
}

static void draw_battery_gauge(GContext *ctx, int pct, bool charging, GColor ink,
                               int y, int x, int w) {
  int left = x + (w - (GAUGE_W + GAUGE_NUB_W)) / 2;
  if (pct < 0) pct = 0;
  if (pct > 100) pct = 100;

  // Red is deliberately not the box's ink: a low battery should shout the same
  // colour whatever accent the wearer has picked. It is not applied while
  // charging, where a low reading is a state the wearer is already fixing.
  bool low = !charging && pct <= GAUGE_LOW_PCT;
  // At zero there is no fill left to colour, so the outline carries the warning.
  GColor outline = (low && pct == 0) ? GColorRed : ink;

  graphics_context_set_stroke_color(ctx, outline);
  graphics_context_set_fill_color(ctx, outline);
  graphics_draw_rect(ctx, GRect(left, y, GAUGE_W, GAUGE_H));
  graphics_fill_rect(ctx, GRect(left + GAUGE_W, y + (GAUGE_H - GAUGE_NUB_H) / 2,
                                GAUGE_NUB_W, GAUGE_NUB_H),
                     0, GCornerNone);

  // Nothing proportional is drawn inside while charging. The percentage is
  // unreliable on the charger -- which is why the row below reads "chg" rather
  // than a number -- and a bar drawn from that same number would be no more
  // trustworthy. A fixed mark says "charging" without claiming a level.
  if (charging) {
    draw_charge_mark(ctx, left + 2, y + 2, ink);
    return;
  }

  // The fill keeps a pixel clear of the outline all round, so a full battery
  // still reads as a filled cell rather than a solid block.
  int fill_w = ((GAUGE_W - 4) * pct + 50) / 100;
  // Anything above empty gets at least a sliver: a real 4% that rounds away to
  // nothing looks exactly like a flat battery.
  if (fill_w == 0 && pct > 0) fill_w = 1;
  if (fill_w > 0) {
    graphics_context_set_fill_color(ctx, low ? GColorRed : ink);
    graphics_fill_rect(ctx, GRect(left + 2, y + 2, fill_w, GAUGE_H - 4), 0, GCornerNone);
  }
}

// The box has to be wider than any line this face can produce, or the text
// wraps *during measurement* and the width comes back capped at the box -- so a
// line that does not fit measures as though it does, and every caller that
// shrinks text to fit is silently defeated.
// The 25x25 icon and the temperature sit side by side, centred in the box as a
// pair. The temperature takes whatever width the icon leaves, which is what
// the size ladder in box_face() is then fitting into.
#define WX_ICON_SIZE 25
#define WX_ICON_GAP 3


static uint32_t wx_resource(uint8_t cond) {
  switch (cond) {
    case WCOND_CLEAR_DAY: return RESOURCE_ID_WEATHER_CLEAR_DAY;
    case WCOND_CLEAR_NIGHT: return RESOURCE_ID_WEATHER_CLEAR_NIGHT;
    case WCOND_CLOUDY: return RESOURCE_ID_WEATHER_CLOUDY_DAY;
    case WCOND_HEAVY_RAIN: return RESOURCE_ID_WEATHER_HEAVY_RAIN;
    case WCOND_HEAVY_SNOW: return RESOURCE_ID_WEATHER_HEAVY_SNOW;
    case WCOND_LIGHT_RAIN: return RESOURCE_ID_WEATHER_LIGHT_RAIN;
    case WCOND_LIGHT_SNOW: return RESOURCE_ID_WEATHER_LIGHT_SNOW;
    case WCOND_PARTLY_CLOUDY_NIGHT: return RESOURCE_ID_WEATHER_PARTLY_CLOUDY_NIGHT;
    case WCOND_PARTLY_CLOUDY: return RESOURCE_ID_WEATHER_PARTLY_CLOUDY;
    case WCOND_RAINING_AND_SNOWING: return RESOURCE_ID_WEATHER_RAINING_AND_SNOWING;
    case WCOND_THUNDERSTORM: return RESOURCE_ID_WEATHER_THUNDERSTORM;
    default: return RESOURCE_ID_WEATHER_GENERIC;
  }
}

// Loaded on first use and kept. Only a handful are ever asked for in a given
// week of weather, so this costs far less than the twelve-icon table it avoids.
static GDrawCommandImage *wx_icon(uint8_t cond) {
  if (cond >= WCOND_COUNT) cond = WCOND_GENERIC;
  if (!s_wx_icon[cond]) {
    s_wx_icon[cond] = gdraw_command_image_create_with_resource(wx_resource(cond));
  }
  return s_wx_icon[cond];
}

// Pebble Draw Commands carry their own colours, so an icon has to be repainted
// before it is drawn in a box whose ink is not the colour it was authored in.
// Taken from TimeStyle's util.c (MIT), which is also where the icons come from.
static bool pdc_recolor_cb(GDrawCommand *command, uint32_t index, void *context) {
  const GColor *c = (const GColor *)context;
  gdraw_command_set_fill_color(command, c[0]);
  gdraw_command_set_stroke_color(command, c[1]);
  return true;
}

static void pdc_recolor(GDrawCommandImage *img, GColor fill, GColor stroke) {
  GColor c[2] = {fill, stroke};
  gdraw_command_list_iterate(gdraw_command_image_get_command_list(img), pdc_recolor_cb, c);
}

// The weather icons are solid shapes, so they take one colour throughout.
static void wx_recolor(GDrawCommandImage *img, GColor ink) { pdc_recolor(img, ink, ink); }

// A stale reading keeps its place but loses its confidence: the same shape in a
// muted ink, so it reads as information we are no longer standing behind. Which
// grey depends on what the box's ink would have been, since the accent fill can
// be light or dark.
static GColor wx_muted(GColor normal) {
  return gcolor_equal(normal, s_bg) ? GColorDarkGray : GColorLightGray;
}

static GSize measure(const char *text, GFont font) {
  return graphics_text_layout_get_content_size(text, font, GRect(0, 0, 1000, 60),
                                               GTextOverflowModeTrailingEllipsis,
                                               GTextAlignmentLeft);
}

// The largest face the band line will fit in, dropping a size at a time rather
// than letting graphics_draw_text ellipsize. Both dates on one line overflow
// Gothic 24 whenever the Hebrew month is a long one, and a truncated date is
// worse than a small one. Everything else in the band fits at 24 and is
// unaffected.
//
// y and lead differ per size so the line stays optically centred in the 38px
// band; 5/LEAD_GOTHIC24 is the value the layout was originally tuned to.
typedef struct {
  GFont font;
  int lead;
  int y;
} BandFace;

// The value row of a footer box, shrunk to fit its third of the screen. Hebrew
// month names are wider than their transliterations -- "Heshvan" fits at 24
// where "adar 1" in Hebrew script does not -- and dy keeps the smaller sizes
// sitting on roughly the same optical centre.
typedef struct {
  GFont font;
  int lead;
  int dy;
} BoxFace;

static BoxFace box_face(const char *text, int width, bool heb) {
  const BoxFace ladder[] = {
      {heb ? s_font_heb24 : s_font_bold24, heb ? LEAD_HEB24 : LEAD_GOTHIC24, 0},
      {heb ? s_font_heb18 : s_font_bold18, heb ? LEAD_HEB18 : 3, 3},
      {heb ? s_font_heb14 : s_font_bold14, heb ? LEAD_HEB14 : LEAD_GOTHIC14, 5},
  };
  const int n = (int)(sizeof(ladder) / sizeof(ladder[0]));
  for (int i = 0; i < n - 1; i++) {
    if (measure(text, ladder[i].font).w <= width) return ladder[i];
  }
  return ladder[n - 1];
}

static BandFace band_face(const char *text, int width) {
  const bool heb = s_settings.hebrew_script;
  const BandFace ladder[] = {
      {heb ? s_font_heb24 : s_font_bold24, heb ? LEAD_HEB24 : LEAD_GOTHIC24, 5},
      {heb ? s_font_heb18 : s_font_bold18, heb ? LEAD_HEB18 : 3, 9},
      {heb ? s_font_heb14 : s_font_bold14, heb ? LEAD_HEB14 : LEAD_GOTHIC14, 12},
  };
  const int n = (int)(sizeof(ladder) / sizeof(ladder[0]));
  for (int i = 0; i < n - 1; i++) {
    if (measure(text, ladder[i].font).w <= width) return ladder[i];
  }
  return ladder[n - 1];
}


// Today's sunset and tzeit, where "today" runs from local midnight.
//
// These used to be read off the ends of the half-day bracket, which pinned them
// to sunrise: from sunset until sunrise the next morning the boxes held the
// sunset that had just passed. Anchoring on midnight instead means the small
// hours show the evening that is coming rather than the one that is over.
//
// Future data beats past data, except that once the next event is more than
// about a day away the past one is more useful again -- and the two are roughly
// equal from around sixteen hours out. Midnight sits comfortably inside that
// indifferent band, so it is a clean cutoff rather than an exact optimum.
//
// time_start_of_today() is a Pebble API and returns local midnight as a UTC
// time_t, which is the basis solar.c works in. Deliberately not mktime(): this
// platform's newlib has burned us twice, in sin() and in strtol().
static void update_solar_times(void) {
  s_have_sunset = false;
  s_have_tzeit = false;

  double midnight_ms = (double)time_start_of_today() * 1000.0;
  double sunset_ms, tzeit_ms;
  if (!solar_next_event(midnight_ms, s_lat, s_lon, SUNRISE_SET_ANGLE, false, &sunset_ms)) return;
  s_sunset_at = (time_t)(sunset_ms / 1000.0);
  s_have_sunset = true;

  // Tzeit is defined by the sunset it follows, so it is found from that sunset
  // rather than independently from midnight: pairing them keeps the two boxes
  // describing the same evening even where a search from midnight would not.
  if (solar_next_event(sunset_ms, s_lat, s_lon, TZEIT_ANGLE, false, &tzeit_ms)) {
    s_tzeit_at = (time_t)(tzeit_ms / 1000.0);
    s_have_tzeit = true;
  }
}

// Ben hashmashot: the stretch between sunset and nightfall, when the countdown
// takes over the shaot line. Both instants come from update_solar_times(), which
// derives tzeit from that same sunset, so the pair always describes one evening
// and the window cannot straddle two. Off outside it, and off entirely wherever
// the sun does not set, where there is nothing to count down to.
static bool countdown_active(time_t now) {
  return s_settings.countdown && s_have_sunset && s_have_tzeit && now >= s_sunset_at &&
         now < s_tzeit_at;
}

static bool kind_needs_next(uint8_t k) {
  return k == SLOT_NEXT_SET_TZEIT || k == SLOT_NEXT_RISE_SET || k == SLOT_NEXT_RISE_SET_TZEIT;
}

static bool any_next_slot(void) {
  return kind_needs_next(s_settings.slot_band) || kind_needs_next(s_settings.slot_left) ||
         kind_needs_next(s_settings.slot_mid) || kind_needs_next(s_settings.slot_right);
}

// The next occurrence of each event, and when this answer expires -- which is
// the moment the soonest of them happens, since that is when "next" changes.
static void update_next_events(time_t now) {
  double now_ms = (double)now * 1000.0;
  double v;
  s_next_from = now;

  s_have_next_rise = solar_next_event(now_ms, s_lat, s_lon, SUNRISE_SET_ANGLE, true, &v);
  if (s_have_next_rise) s_next_rise = (time_t)(v / 1000.0);
  s_have_next_set = solar_next_event(now_ms, s_lat, s_lon, SUNRISE_SET_ANGLE, false, &v);
  if (s_have_next_set) s_next_set = (time_t)(v / 1000.0);
  s_have_next_tz = solar_next_event(now_ms, s_lat, s_lon, TZEIT_ANGLE, false, &v);
  if (s_have_next_tz) s_next_tz = (time_t)(v / 1000.0);

  // Somewhere with no crossings at all, do not spin: try again in an hour.
  s_next_stale = now + 3600;
  if (s_have_next_rise && s_next_rise < s_next_stale) s_next_stale = s_next_rise;
  if (s_have_next_set && s_next_set < s_next_stale) s_next_stale = s_next_set;
  if (s_have_next_tz && s_next_tz < s_next_stale) s_next_stale = s_next_tz;
  s_next_stale += 1;  // strictly past it, so the event is not found again
}

// Recompute the half-day bracket, the sunset/tzeit strings and the Hebrew date
// when the bracket ends or a new day starts.
//
// **Never call this from a layer update proc.** It runs the NOAA solar maths in
// soft-float, several hundred bytes of stack and a good deal of work, and an
// update proc is already deep inside the firmware's render path with a render
// watchdog running. Doing it there made the watchface crash intermittently on
// real hardware -- most visibly right after a settings change, which
// invalidates the bracket and so forces the recompute into the very next frame.
// The emulator tolerated it, and so did the probe, which called the same code
// from a timer callback where the stack is shallow.
//
// Callers are the tick handler and the message handler; drawing only ever reads
// what this leaves behind.
// Shabbat and yom tov. Unlike the Hebrew date this cannot be cached until the
// next day boundary: two of the four clauses turn on the hour and on whether
// nightfall has passed, so it is recomputed on every tick. That costs a scan of
// thirteen dates and a few comparisons, which is nothing next to the solar
// maths beside it -- and like everything else here it stays out of the update
// proc, which only ever reads what this leaves behind.
//
// With no location, or at a latitude where the sun does not set, the answer is
// SHABBAT_NONE. That is a deliberate choice of which way to fail rather than an
// oversight: a watch that had lost its fix would otherwise behave as though it
// were Shabbat indefinitely, with no way for the wearer to say otherwise. It
// wants revisiting once something actually depends on the answer.
static void update_shabbat(const struct tm *lt, time_t now) {
  if (!s_br.valid || !s_have_tzeit) {
    s_shabbat = SHABBAT_NONE;
    return;
  }
  const ShabbatNow n = {
      .heb_month = s_heb.month,
      .heb_day = s_heb.day,
      .wday = lt->tm_wday,
      .hour = lt->tm_hour,
      .sun_is_up = s_br.is_day,
      .before_tzeit = now < s_tzeit_at,
      .second_days = s_settings.second_days,
  };
  s_shabbat = shabbat_kind(&n);
}

static void refresh(time_t now) {
  // Ahead of every early exit: this one does not depend on having a location,
  // and a weather box is drawn from it whether or not the solar maths worked.
  s_wx_stale = weather_is_stale(&s_wx, (int32_t)now);
  // The early exits below all mean "not known", and leaving a stale answer
  // behind would be worse than saying so.
  s_shabbat = SHABBAT_NONE;
  if (!s_have_location) return;
  double now_ms = (double)now * 1000.0;

  if (!s_br.valid || now_ms >= s_br.end_ms || now_ms < s_br.start_ms) {
    s_br = solar_bracket(now_ms, s_lat, s_lon);
    // Forces the block below: a bracket flip rolls the Hebrew date, and it is
    // also the one moment the boxes are worth rechecking off a day boundary.
    s_last_day = -1;
  }

  // localtime() hands back a pointer to one static struct tm, so anything that
  // calls it again invalidates what is held here. Take a copy rather than trust
  // that nothing below does.
  struct tm *now_tm = localtime(&now);
  if (!now_tm) return;
  struct tm lt = *now_tm;
  if (s_br.valid && lt.tm_mday != s_last_day) {
    s_last_day = lt.tm_mday;
    s_heb = hebdate_for_now(lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday,
                            lt.tm_hour, s_br.is_day);
    update_solar_times();
  }
  update_shabbat(&lt, now);

  // The combined slots expire on their own schedule -- when the event they are
  // showing happens -- rather than at midnight, and cost three more passes of
  // the solar maths, so they are only computed while one is configured.
  if (any_next_slot() && (now >= s_next_stale || now < s_next_from)) update_next_events(now);
}

static void draw_face(Layer *layer, GContext *ctx) {
  // Two rectangles, and the difference matters. bounds is the whole screen and
  // is what the background must cover, or the area under an appearing Timeline
  // Peek shows whatever was there before. vis is the part not covered by an
  // overlay, and is what content has to fit inside.
  //
  // No subscription is needed for this: the app is redrawn automatically
  // whenever the unobstructed area changes. Peek animates in with a bounce, so
  // vis briefly reports heights either side of its resting value -- the layout
  // below has to degrade continuously rather than switch between two cases.
  GRect bounds = layer_get_bounds(layer);
  GRect vis = layer_get_unobstructed_bounds(layer);
  int vis_bottom = vis.origin.y + vis.size.h;
  time_t now = time(NULL);
  // Draw only. See refresh(): the solar maths must not run from here.

  graphics_context_set_fill_color(ctx, s_bg);
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);

  if (!s_have_location || !s_br.valid) {
    // Say what is wrong rather than showing a stale or invented time. No
    // location means the phone has not reported one yet; a location with no
    // bracket means a polar latitude where the sun does not cross today.
    draw_centered(ctx, s_have_location ? "no sun window" : "waiting for phone",
                  s_font_bold24, LEAD_GOTHIC24, s_dim, (vis.size.h - 28) / 2, 0,
                  bounds.size.w);
    return;
  }

  // A copy, not the pointer. localtime() returns one static struct tm, and
  // format_hhmm() calls localtime() too -- so a solar slot drawn before the
  // civil line would otherwise rewrite the time underneath it, and the clock
  // would show the sunset. That is a settings change away from happening.
  struct tm *now_tm = localtime(&now);
  if (!now_tm) return;
  struct tm lt = *now_tm;

  // Band
  graphics_context_set_fill_color(ctx, s_accent);
  graphics_fill_rect(ctx, GRect(0, 0, bounds.size.w, BAND_H), 0, GCornerNone);

  char band[68];  // both parts plus ": " and the NUL
  band_content(s_settings.slot_band, &lt, band, sizeof(band));
  BandFace bf = band_face(band, bounds.size.w - 6);
  draw_centered(ctx, band, bf.font, bf.lead, s_on_accent, bf.y, 0, bounds.size.w);

  // Civil time. Seconds are only shown when they are actually kept up to date:
  // a frozen seconds field is worse than none. Ticking once a minute, the
  // 12-hour clock spends that space on am/pm instead, and the 24-hour clock,
  // which does not need it, simply centres what is left.
  char civil[16];
  bool h24 = use_24h();
  const char *meridiem = NULL;
  int hour = lt.tm_hour;
  if (!h24) {
    hour %= 12;
    if (hour == 0) hour = 12;
  }
  if (s_settings.tick_seconds) {
    snprintf(civil, sizeof(civil), h24 ? "%02d:%02d:%02d" : "%d:%02d:%02d", hour, lt.tm_min,
             lt.tm_sec);
  } else {
    snprintf(civil, sizeof(civil), h24 ? "%02d:%02d" : "%d:%02d", hour, lt.tm_min);
    if (!h24) meridiem = (lt.tm_hour < 12) ? "am" : "pm";
  }

  if (meridiem) {
    // The civil faces are numeral subsets with no letters in them at all, so the
    // meridiem has to be set in Gothic beside the clock rather than appended to
    // it. Both runs are measured and the pair centred together; the boxes are
    // bottom-aligned, which keeps the small text sitting on the baseline of the
    // large whichever civil face is selected.
    GSize ts = measure(civil, s_font_civil);
    GSize ms = measure(meridiem, s_font_bold24);
    int total = ts.w + MERIDIEM_GAP + ms.w;
    int x = (bounds.size.w - total) / 2;
    int civil_box_top = CIVIL_Y - s_lead_civil;
    draw_at(ctx, civil, s_font_civil, s_lead_civil, s_fg, CIVIL_Y, x, ts.w + 2);
    draw_at(ctx, meridiem, s_font_bold24, LEAD_GOTHIC24, s_dim,
            civil_box_top + (ts.h - ms.h) + LEAD_GOTHIC24 - MERIDIEM_NUDGE,
            x + ts.w + MERIDIEM_GAP, ms.w + 2);
  } else {
    draw_centered(ctx, civil, s_font_civil, s_lead_civil, s_fg, CIVIL_Y, 0, bounds.size.w);
  }

  // The shaot line, or the countdown in its place. Between sunset and nightfall
  // the proportional reading is at its least useful -- it has just rolled over
  // and barely moves -- and the wait for nightfall is the thing actually being
  // watched, so the countdown takes the line rather than a box. Ordinary
  // seconds, not chalakim: it is a wall-clock wait, and the label says so.
  char shaot[16];
  const bool counting = countdown_active(now);
  if (counting) {
    shaot_format_countdown((int)(s_tzeit_at - now), shaot, sizeof(shaot));
    draw_centered(ctx, "till nightfall", s_font_label, LEAD_GOTHIC14, s_dim, COUNTDOWN_LABEL_Y, 0,
                  bounds.size.w);
  } else {
    // Ticking once a minute the reading would otherwise be up to a whole minute
    // stale, always in the same direction; half a minute ahead makes it right on
    // average across the minute it sits unchanged.
    double shaot_ms = (double)now * 1000.0;
    if (!s_settings.tick_seconds) shaot_ms += 30000.0;
    shaot_format(shaot_chalakim_now(shaot_ms, s_br.start_ms, s_br.end_ms),
                 s_settings.offset6, s_settings.with_minutes, shaot, sizeof(shaot));
  }
  if (counting) {
    // Sized to the widest ordinary reading rather than to the digits on screen,
    // so the block does not step narrower when the minutes drop to one digit.
    // A window longer than an hour formats as H:MM:SS, which is wider still --
    // measure the string too, so that grows the block instead of spilling out.
    const int now_w = measure(shaot, s_font_shaot).w;
    const int wide_w = measure("00:00", s_font_shaot).w;
    const int bw = (now_w > wide_w ? now_w : wide_w) + 2 * COUNTDOWN_BOX_PAD;

    // Centred, then pushed left only as far as the disconnect icon requires.
    // The nudge does not depend on whether the phone is actually connected: a
    // block that slid sideways when Bluetooth dropped would draw the eye to the
    // wrong thing, and the icon is the one that should be doing that.
    const int icon_left = bounds.size.w - BT_BOX - 2 - COUNTDOWN_BT_GAP;
    int bx = (bounds.size.w - bw) / 2;
    if (bx + bw > icon_left) bx = icon_left - bw;
    if (bx < 0) bx = 0;

    graphics_context_set_fill_color(ctx, s_accent);
    graphics_fill_rect(ctx, GRect(bx, COUNTDOWN_BOX_TOP, bw, COUNTDOWN_BOX_BOTTOM - COUNTDOWN_BOX_TOP),
                       0, GCornerNone);
    draw_centered(ctx, shaot, s_font_shaot, LEAD_LECO42, s_on_accent, 112, bx, bw);
  } else {
    draw_centered(ctx, shaot, s_font_shaot, LEAD_LECO42, s_fg, 112, 0, bounds.size.w);
  }

  // Footer. Anchored to the bottom of the unobstructed area, so it rides up
  // ahead of an appearing Peek instead of disappearing under it, and is dropped
  // once there is no longer room for it clear of the shaot line. The three
  // slots are the only thing Peek can cost the wearer; everything above stays
  // exactly where it was.
  int footer_top = vis_bottom - FOOTER_ZONE_H;
  if (footer_top < SHAOT_INK_BOTTOM + FOOTER_MIN_GAP) return;

  // The accent fill belongs to the outer slot positions, not to their content:
  // every slot is user-configurable, only the fill colour is.
  int footer_h = vis_bottom - footer_top - 1;
  int third = bounds.size.w / 3;
  const uint8_t kinds[3] = {s_settings.slot_left, s_settings.slot_mid, s_settings.slot_right};

  // Content first, because the widths depend on it.
  char labels[3][24], values[3][40];
  SlotLayout layouts[3];
  for (int i = 0; i < 3; i++) {
    layouts[i] = slot_content(kinds[i], &lt, false, labels[i], sizeof(labels[i]), values[i],
                              sizeof(values[i]));
  }

  // A month name is the only thing a box can hold that outgrows a third of the
  // screen, and everything it sits beside -- a time, a gauge -- has slack to
  // spare. So when exactly one box is showing one, it takes the width its name
  // actually measures and the other two give up the difference equally. Two
  // date boxes at once would have nothing to borrow from, so the split stays
  // even and box_face() shrinks the type instead.
  int widths[3] = {third + 1, third + 1, bounds.size.w - 2 * third};
  int date_box = -1, date_boxes = 0;
  for (int i = 0; i < 3; i++) {
    if (kinds[i] == SLOT_HEBREW) {
      date_box = i;
      date_boxes++;
    }
  }
  if (date_boxes == 1) {
    bool heb = s_settings.hebrew_script;
    int want = measure(values[date_box], heb ? s_font_heb24 : s_font_bold24).w + BOX_PAD;
    if (want > BOX_WIDE_MAX) want = BOX_WIDE_MAX;
    int narrow = (bounds.size.w - want) / 2;
    if (want > widths[date_box] && narrow >= BOX_NARROW_MIN) {
      for (int i = 0; i < 3; i++) widths[i] = (i == date_box) ? want : narrow;
    }
  }

  int xs[3] = {0, widths[0], widths[0] + widths[1]};
  widths[2] = bounds.size.w - xs[2];  // the last box absorbs any rounding

  // The outer two boxes carry the accent fill. While a weather box is showing
  // the other half of itself, it swaps: an outer one is drawn on the background
  // and the middle one takes the fill. With the label naming the day, that is
  // the pair of cues that says which half you are looking at.
  //
  // The pinned forecast never swaps, for the reason the swap exists: it means
  // "this box is showing the other half of itself just now", which a box
  // configured that way permanently never is.
  bool filled[3];
  for (int i = 0; i < 3; i++) {
    filled[i] = (i != 1);
    if (wx_swapped() && kinds[i] == SLOT_WEATHER) filled[i] = !filled[i];
  }

  graphics_context_set_fill_color(ctx, s_rule);
  graphics_fill_rect(ctx, GRect(0, footer_top, bounds.size.w, 1), 0, GCornerNone);
  graphics_context_set_fill_color(ctx, s_accent);
  for (int i = 0; i < 3; i++) {
    // The leftmost fill runs one pixel wide to close the seam against its
    // neighbour; the others start where the previous box ended.
    if (filled[i]) {
      graphics_fill_rect(ctx, GRect(xs[i], footer_top + 1, widths[i] + (i == 0 ? 1 : 0), footer_h),
                         0, GCornerNone);
    }
  }

  for (int i = 0; i < 3; i++) {
    bool on_fill = filled[i];
    GColor ink = on_fill ? s_on_accent : s_fg;
    int x = xs[i];
    int w = widths[i];

    SlotLayout layout = layouts[i];
    const char *label = labels[i];
    const char *value = values[i];
    // A Hebrew month name can only appear in the value row, and only when the
    // wearer has asked for Hebrew script. The label row above it is always
    // Latin ("30th of"), so it stays Gothic.
    // A weather box gives up the icon's width before the ladder starts fitting
    // the temperature into what is left.
    const int value_w =
        (layout == SLOT_LAYOUT_WEATHER) ? w - WX_ICON_SIZE - WX_ICON_GAP : w;
    BoxFace vf = box_face(value, value_w, s_settings.hebrew_script && slot_has_hebrew(kinds[i]));
    if (layout == SLOT_LAYOUT_SPLIT) {
      // A date split over both lines: same size and weight, no label.
      draw_centered(ctx, label, s_font_bold24, LEAD_GOTHIC24, ink, footer_top + 3, x, w);
      draw_centered(ctx, value, vf.font, vf.lead, ink, footer_top + 29 + vf.dy, x, w);
    } else {
      // The gauge occupies the label row, which is why the box needs no
      // retuning: the percentage stays exactly where the value always sat.
      if (layout == SLOT_LAYOUT_WEATHER) {
        const GColor wink = s_wx_stale ? wx_muted(ink) : ink;
        const GColor lab = s_wx_stale ? wink : (on_fill ? s_on_accent : s_dim);
        const bool fc = kind_shows_forecast(kinds[i]);
        const int day = fc ? weather_pick_day(&s_wx, wx_wanted_ymd(&lt)) : -1;
        const bool have = fc ? (day >= 0) : (bool)s_wx.have_current;
        GDrawCommandImage *icon = have ? wx_icon(fc ? s_wx.day_cond[day] : s_wx.cond) : NULL;
        if (icon) wx_recolor(icon, wink);

        if (!have) {
          draw_centered(ctx, label, s_font_label, LEAD_GOTHIC14, lab, footer_top + 6, x, w);
          draw_centered(ctx, value, vf.font, vf.lead, wink, footer_top + 24 + vf.dy, x, w);
          continue;
        }

        if (!fc) {
          // Current conditions are one number, which fits beside the icon on a
          // single line at 18pt.
          draw_centered(ctx, label, s_font_label, LEAD_GOTHIC14, lab, footer_top + 6, x, w);
          const int text_w = measure(value, vf.font).w;
          const int left = x + (w - (WX_ICON_SIZE + WX_ICON_GAP + text_w)) / 2;
          if (icon) gdraw_command_image_draw(ctx, icon, GPoint(left, footer_top + 26));
          draw_at(ctx, value, vf.font, vf.lead, wink, footer_top + 24 + vf.dy,
                  left + WX_ICON_SIZE + WX_ICON_GAP, w);
        } else {
          // The forecast is two numbers, and beside the icon on one line they
          // drop to 14pt -- too small to read at a glance. Putting the icon and
          // the day side by side on the top row instead frees the whole width
          // beneath for one full-size line. Stacking the two temperatures under
          // a header would read better still, but a header and two 24pt lines
          // need 62px and the box is 57, which is what clipped every attempt.
          //
          // The day is not decoration: the forecast rolls from today to
          // tomorrow at the cutoff, so without the word there is no telling
          // whose high is on screen.
          if (icon) gdraw_command_image_draw(ctx, icon, GPoint(x + 2, footer_top + 2));
          draw_centered(ctx, label, s_font_label, LEAD_GOTHIC14, lab, footer_top + 9,
                        x + WX_ICON_SIZE + 2, w - WX_ICON_SIZE - 4);
          // Wide readings -- a three-digit high, or a negative low -- still
          // outgrow 24pt, and the ladder drops them a size rather than clipping.
          BoxFace f = box_face(value, w - 4, false);
          draw_centered(ctx, value, f.font, f.lead, wink, footer_top + 30 + f.dy, x, w);
        }
        continue;
      }
      if (layout == SLOT_LAYOUT_GAUGE) {
        draw_battery_gauge(ctx, s_battery, s_charging, ink, footer_top + 6, x, w);
      } else {
        draw_centered(ctx, label, s_font_label, LEAD_GOTHIC14, on_fill ? s_on_accent : s_dim,
                      footer_top + 6, x, w);
      }
      draw_centered(ctx, value, vf.font, vf.lead, ink, footer_top + 24 + vf.dy, x, w);
    }
  }
}

// The disconnect indicator: the Bluetooth rune with a strike through it, drawn
// in the right-hand gutter between the clock and the shaot line. That gutter is
// dead space at every time of day because the clock is centred, which is why
// the indicator is an overlay and belongs to none of the five regions.
//
// The strike is what makes it self-describing. A plain rune is the symbol for
// Bluetooth *working* almost everywhere else, and while this one only ever
// appears when the phone is gone, a glance should not have to know that.

// One stroke: up the left diagonal, down the stem, back out the other diagonal.
// Drawn rather than loaded because it has to read at 25px, and TimeStyle's
// icon -- a phone with a cross -- carries more detail than survives at that
// size. Below about 20px the diagonals collapse and it stops reading at all.
static void bt_rune(GContext *ctx, int cx, int top, int h) {
  const int r = (h * 3) / 10;
  const int y0 = top, y1 = top + (h * 3) / 10, y3 = top + (h * 7) / 10, y4 = top + h;
  const int xl = cx - r, xr = cx + r;
  graphics_draw_line(ctx, GPoint(xl, y1), GPoint(xr, y3));
  graphics_draw_line(ctx, GPoint(xr, y3), GPoint(cx, y4));
  graphics_draw_line(ctx, GPoint(cx, y4), GPoint(cx, y0));
  graphics_draw_line(ctx, GPoint(cx, y0), GPoint(xr, y1));
  graphics_draw_line(ctx, GPoint(xr, y1), GPoint(xl, y3));
}

// The row the gutter overlays sit on. Measured from the *full* bounds, never
// from the unobstructed area: the gutters are dead space at full height, so
// there is nothing for these to get out of the way of, and anchoring them to
// the visible area made the disconnect icon jump 29px up the screen whenever a
// Timeline Peek appeared. An indicator that moves when unrelated news arrives
// reads as news itself. The resting rows are far above the peek, so holding
// still costs no visibility.
static int gutter_top(GRect bounds) {
  const int footer_top = bounds.size.h - FOOTER_ZONE_H;
  return (BAND_H + footer_top) / 2 - BT_BOX / 2;
}

static void draw_bt_overlay(GContext *ctx, GRect bounds) {
  if (!s_settings.bt_icon || s_bt_connected) return;

  const int x = bounds.size.w - BT_BOX - 2;
  const int y = gutter_top(bounds);

  graphics_context_set_stroke_color(ctx, s_fg);
  graphics_context_set_stroke_width(ctx, 2);
  bt_rune(ctx, x + BT_BOX / 2, y + 1, 23);
  graphics_draw_line(ctx, GPoint(x + 1, y + 23), GPoint(x + 23, y + 1));
  graphics_context_set_stroke_width(ctx, 1);
}

// The low-battery indicator: an empty battery outline in the left-hand gutter,
// mirroring the disconnect icon opposite it. Red rather than the face's ink,
// for the same reason the footer gauge goes red -- a flat battery should shout
// the same colour whatever accent the wearer has picked.
//
// The cell is drawn empty. A proportional fill would be a second, smaller
// battery gauge competing with the one a footer box may already be showing;
// this one is not a reading, it is a warning, and the number is available in a
// box for anyone who wants it. The terminal nub stays on the right even though
// the icon is mirrored across the screen: a battery with its terminal on the
// left stops looking like a battery.
#define LOWBATT_W 23      // the body, excluding the nub
#define LOWBATT_H 13
#define LOWBATT_NUB_W 2
#define LOWBATT_NUB_H 7

static void draw_lowbatt_overlay(GContext *ctx, GRect bounds) {
  if (!s_settings.low_batt_icon) return;
  // Not while charging: a low reading on the charger is a state the wearer is
  // already fixing, and the footer gauge suppresses its red for the same
  // reason. Same threshold as the gauge, so the two never disagree.
  if (s_charging || s_battery > GAUGE_LOW_PCT) return;

  const int x = 2;
  const int y = gutter_top(bounds) + (BT_BOX - LOWBATT_H) / 2;

  graphics_context_set_stroke_color(ctx, GColorRed);
  graphics_context_set_fill_color(ctx, GColorRed);
  // Two nested rects, not one with a stroke width: graphics_draw_rect() ignores
  // the context's stroke width, so asking for 2px silently draws 1 -- and a 1px
  // outline is visibly lighter than the 2px disconnect rune facing it across
  // the screen. Measured, not eyeballed; the two are meant to weigh the same.
  graphics_draw_rect(ctx, GRect(x, y, LOWBATT_W, LOWBATT_H));
  graphics_draw_rect(ctx, GRect(x + 1, y + 1, LOWBATT_W - 2, LOWBATT_H - 2));
  graphics_fill_rect(ctx, GRect(x + LOWBATT_W, y + (LOWBATT_H - LOWBATT_NUB_H) / 2,
                                LOWBATT_NUB_W, LOWBATT_NUB_H),
                     0, GCornerNone);
}

static void canvas_update(Layer *layer, GContext *ctx) {
  draw_face(layer, ctx);
  // After the face, and outside it: draw_face() gives up early when the
  // unobstructed area is too short for the footer, and a gutter indicator that
  // vanishes under a Timeline Peek is not doing its job.
  const GRect bounds = layer_get_bounds(layer);
  draw_bt_overlay(ctx, bounds);
  draw_lowbatt_overlay(ctx, bounds);
}

// --- services ---------------------------------------------------------------

static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  // Recompute here rather than while drawing. refresh() is cheap on the vast
  // majority of ticks: it only does real work when the bracket has run out or
  // the day has rolled.
  refresh(time(NULL));
  // Re-checked every tick because the countdown's window opens and closes on
  // its own, with no event to hang it off. On a minute tick that means the
  // countdown can appear up to a minute after sunset, which is the same lag
  // everything else on the face already has in that mode.
  subscribe_tick();
  // Once an hour, at a minute of this watch's own choosing -- but every five
  // minutes while the phone is reachable and the box has nothing current to
  // show. Waiting out the rest of the hour to discover that is too long to sit
  // looking at an empty or hours-old box.
  //
  // Stale counts as well as empty, and that is the whole point of the second
  // term: after a night with Bluetooth off the watch does have weather, just
  // old weather, so a gate reading only have_current would leave the morning
  // -- the one time this matters most -- to the hourly schedule. The chase in
  // request_weather() is what usually catches this within seconds; this is the
  // floor under it, for a reconnect that arrived while the chase was already
  // spent.
  //
  // Gated on the link, because asking across a dead one spends a wake to reach
  // nobody, and the connection handler already asks the moment it is back.
  if (tick_time->tm_sec == 0) {
    const bool waiting = s_bt_connected && (!s_wx.have_current || s_wx_stale);
    if (tick_time->tm_min == s_wx_minute || (waiting && tick_time->tm_min % 5 == 0)) {
      request_weather();
    }
  }
  layer_mark_dirty(s_canvas);
}

static void battery_handler(BatteryChargeState state) {
  s_battery = state.charge_percent;
  // is_charging, not is_plugged: a full battery sitting on the charger has a
  // trustworthy reading and should keep showing it.
  s_charging = state.is_charging;
  layer_mark_dirty(s_canvas);
}

// Ask the phone for weather. The watch drives this rather than the phone
// pushing on a timer, because only the watch knows whether any slot is
// currently showing weather -- there is no point spending a radio wake and an
// HTTP fetch on a face that is not displaying it. The phone treats any message
// from us as the request; we send nothing else.
static void wx_retry(void *data);

// Arm, re-arm or stand down the chase. Called with the number of requests that
// have gone unanswered so far; 0 stands it down, which is what a payload does.
static void wx_schedule_retry(uint8_t attempts) {
  if (s_wx_retry_timer) {
    app_timer_cancel(s_wx_retry_timer);
    s_wx_retry_timer = NULL;
  }
  s_wx_attempts = attempts;
  const uint32_t ms = weather_retry_ms(attempts);
  if (ms == 0) return;
  s_wx_retry_timer = app_timer_register(ms, wx_retry, NULL);
}

static void send_weather_request(void) {
  DictionaryIterator *iter;
  if (app_message_outbox_begin(&iter) != APP_MSG_OK) return;
  dict_write_uint8(iter, 0, 0);
  app_message_outbox_send();
}

static void wx_retry(void *data) {
  s_wx_retry_timer = NULL;
  // Nothing to chase across a dead link. The chase is not resumed here either,
  // because the connection handler starts a fresh one the moment the phone is
  // back, which is sooner than any delay left in this schedule.
  if (!s_bt_connected || !any_weather_slot()) return;
  send_weather_request();
  wx_schedule_retry((uint8_t)(s_wx_attempts + 1));
}

static void request_weather(void) {
  if (!any_weather_slot()) return;
  send_weather_request();
  // Start the count over. Every caller of this is a fresh reason to want
  // weather -- a reconnect, a settings change, the hourly slot -- and each
  // deserves the full schedule rather than the tail of an older one.
  wx_schedule_retry(1);
}

static void alt_view_timeout(void *data) {
  s_alt_timer = NULL;
  s_alt_view = false;
  layer_mark_dirty(s_canvas);
}

// The only input a watchface gets: screen touch is never delivered to one, as
// README.md records. That is also why the view reverts on its own -- some of
// these taps are a jostled wrist rather than a decision, and a latching mode
// would sit there until the next one.
// The first thing s_shabbat is actually used for. Kept out of tap_has_effect(),
// which answers a question about how the face is configured and is the same all
// week; this one is about the moment, and reads better where the gesture is
// handled than folded into a predicate about slots.
//
// The subscription is left in place rather than torn down and rebuilt at every
// boundary: the handler returning early is the same outcome for a fraction of
// the bookkeeping, and it means nothing has to notice the exact second Shabbat
// ends in order to give the gesture back.
static bool taps_suppressed(void) {
  return s_settings.shabbat_no_taps && s_shabbat != SHABBAT_NONE;
}

static void accel_tap_handler(AccelAxisType axis, int32_t direction) {
  if (taps_suppressed()) return;
  if (!tap_has_effect()) return;
  if (s_alt_timer) {
    app_timer_cancel(s_alt_timer);
    s_alt_timer = NULL;
  }
  s_alt_view = !s_alt_view;
  if (s_alt_view) {
    s_alt_timer = app_timer_register(ALT_VIEW_HOLD_MS, alt_view_timeout, NULL);
  }
  layer_mark_dirty(s_canvas);
}

static void connection_handler(bool connected) {
  s_bt_connected = connected;
  // Reconnecting is the first chance to catch up on whatever was missed while
  // the phone was away, and it costs nothing when the data is already current.
  if (connected) request_weather();
  layer_mark_dirty(s_canvas);
}

static void subscribe_tick(void) {
  // SECOND_UNIT is deliberate: per-second chalakim is the point of this face,
  // and the tick rate is a user setting. Do not "optimise" this to MINUTE_UNIT.
  //
  // The countdown overrides that setting for as long as it is on screen: it
  // counts wall-clock seconds, and one that only moved once a minute would be
  // worse than not showing it. That is under an hour a day, and only for a
  // wearer who asked for the countdown at all.
  TimeUnits want = (s_settings.tick_seconds || countdown_active(time(NULL))) ? SECOND_UNIT
                                                                            : MINUTE_UNIT;
  if (s_tick_unit == want) return;
  s_tick_unit = want;
  tick_timer_service_subscribe(want, tick_handler);
}

// --- messages ---------------------------------------------------------------
// One key per setting, delivered by Clay. The old JavaScript build had to pack
// all of them into a single string because per-key messages cost it more mod
// memory than it had; in C the dictionary is ordinary.

// The phone side picks the narrowest integer that fits, so a tuple carrying a
// boolean or a small enum is one byte, not four. Reading value->int32 from it
// would take the following bytes of the dictionary as the high end of the
// number, which is how a "0" arrives as something else entirely.
//
// Strings have to be accepted as well. Clay's select component reads its value
// off a DOM <select>, whose value is always a string, and
// Clay.prepareForAppMessage converts only numbers and booleans -- everything
// else passes through untouched. So a select arrives as text however its
// options are declared, and rejecting that silently drops the setting.
// Convert one tuple to an integer, whatever shape it arrived in.
//
// Three shapes have to be handled. The phone picks the narrowest integer that
// fits, so a boolean or a small enum is one byte, not four -- reading
// value->int32 from it would take the following bytes of the dictionary as the
// high end of the number. Clay's select components send **strings**: the
// component reads a DOM <select>, whose value is always a string, and
// Clay.prepareForAppMessage converts only numbers and booleans, so declaring
// numeric options in the config changes nothing.
static bool tuple_to_int(const Tuple *t, int32_t *out) {
  if (!t) return false;
  switch (t->type) {
    case TUPLE_CSTRING:
      // numparse_int rather than strtol: see numparse.h.
      return numparse_int(t->value->cstring, out);
    case TUPLE_INT:
      switch (t->length) {
        case 1: *out = t->value->int8; return true;
        case 2: *out = t->value->int16; return true;
        case 4: *out = t->value->int32; return true;
        default: return false;
      }
    case TUPLE_UINT:
      switch (t->length) {
        case 1: *out = t->value->uint8; return true;
        case 2: *out = t->value->uint16; return true;
        case 4: *out = (int32_t)t->value->uint32; return true;
        default: return false;
      }
    default:
      return false;
  }
}

// One pass over the dictionary, switching on the key, rather than a dict_find()
// search per setting. Same result for a well-formed message, but it does not
// depend on the iterator surviving eleven searches, it visits each tuple
// exactly once, and every key is handled in one place where the shapes above
// are easy to keep straight.
static void inbox_received(DictionaryIterator *iter, void *context) {
  int32_t v;
  // The phone re-sends the stored settings on every launch, so "a settings key
  // arrived" is no longer the same as "something changed". Keep a copy and
  // compare, or every launch would rewrite persistent storage to no effect.
  const Settings before = s_settings;
  bool settings_changed = false;
  bool weather_changed = false;
  int32_t day_ymd[WEATHER_DAYS] = {0};
  bool have_lat = false, have_lon = false;
  int32_t lat_raw = 0, lon_raw = 0;

  // MESSAGE_KEY_* are extern constants rather than literals, so this is an
  // if/else chain and not a switch.
  for (Tuple *t = dict_read_first(iter); t; t = dict_read_next(iter)) {
    uint32_t k = t->key;

    // Coordinates arrive scaled by 1e6; AppMessage carries no floating point.
    if (k == MESSAGE_KEY_LAT) {
      if (tuple_to_int(t, &v)) { lat_raw = v; have_lat = true; }
    } else if (k == MESSAGE_KEY_LON) {
      if (tuple_to_int(t, &v)) { lon_raw = v; have_lon = true; }

    } else if (k == MESSAGE_KEY_Offset6) {
      if (tuple_to_int(t, &v)) { s_settings.offset6 = (v != 0); settings_changed = true; }
    } else if (k == MESSAGE_KEY_WithMinutes) {
      if (tuple_to_int(t, &v)) { s_settings.with_minutes = (v != 0); settings_changed = true; }
    } else if (k == MESSAGE_KEY_TickSeconds) {
      if (tuple_to_int(t, &v)) { s_settings.tick_seconds = (v != 0); settings_changed = true; }
    } else if (k == MESSAGE_KEY_Countdown) {
      if (tuple_to_int(t, &v)) { s_settings.countdown = (v != 0); settings_changed = true; }
    } else if (k == MESSAGE_KEY_HebrewScript) {
      if (tuple_to_int(t, &v)) { s_settings.hebrew_script = (v != 0); settings_changed = true; }
    } else if (k == MESSAGE_KEY_CivilFont) {
      if (tuple_to_int(t, &v)) { s_settings.civil_font = (uint8_t)v; settings_changed = true; }
    } else if (k == MESSAGE_KEY_SlotBand) {
      if (tuple_to_int(t, &v)) { s_settings.slot_band = (uint8_t)v; settings_changed = true; }
    } else if (k == MESSAGE_KEY_SlotLeft) {
      if (tuple_to_int(t, &v)) { s_settings.slot_left = (uint8_t)v; settings_changed = true; }
    } else if (k == MESSAGE_KEY_SlotMid) {
      if (tuple_to_int(t, &v)) { s_settings.slot_mid = (uint8_t)v; settings_changed = true; }
    } else if (k == MESSAGE_KEY_SlotRight) {
      if (tuple_to_int(t, &v)) { s_settings.slot_right = (uint8_t)v; settings_changed = true; }
    } else if (k == MESSAGE_KEY_Metric) {
      if (tuple_to_int(t, &v)) { s_settings.metric = (v != 0); settings_changed = true; }
    } else if (k == MESSAGE_KEY_DisconnectIcon) {
      if (tuple_to_int(t, &v)) { s_settings.bt_icon = (v != 0); settings_changed = true; }
    } else if (k == MESSAGE_KEY_LowBatteryIcon) {
      if (tuple_to_int(t, &v)) { s_settings.low_batt_icon = (v != 0); settings_changed = true; }
    } else if (k == MESSAGE_KEY_SecondDays) {
      if (tuple_to_int(t, &v)) { s_settings.second_days = (v != 0); settings_changed = true; }
    } else if (k == MESSAGE_KEY_ShabbatSuppressTaps) {
      if (tuple_to_int(t, &v)) { s_settings.shabbat_no_taps = (v != 0); settings_changed = true; }

    // Weather. Temperatures arrive in Celsius whatever the units setting says,
    // so switching units redraws immediately instead of waiting for a fetch.
    } else if (k == MESSAGE_KEY_WxTemp) {
      if (tuple_to_int(t, &v)) {
        s_wx.temp_c = (int16_t)v;
        s_wx.have_current = 1;
        weather_changed = true;
      }
    } else if (k == MESSAGE_KEY_WxCond) {
      if (tuple_to_int(t, &v)) { s_wx.cond = (uint8_t)v; weather_changed = true; }
    } else if (k == MESSAGE_KEY_WxDay0Ymd) {
      if (tuple_to_int(t, &v)) { day_ymd[0] = v; }
    } else if (k == MESSAGE_KEY_WxDay0High) {
      if (tuple_to_int(t, &v)) { s_wx.day_high_c[0] = (int16_t)v; }
    } else if (k == MESSAGE_KEY_WxDay0Low) {
      if (tuple_to_int(t, &v)) { s_wx.day_low_c[0] = (int16_t)v; }
    } else if (k == MESSAGE_KEY_WxDay0Cond) {
      if (tuple_to_int(t, &v)) { s_wx.day_cond[0] = (uint8_t)v; }
    } else if (k == MESSAGE_KEY_WxDay1Ymd) {
      if (tuple_to_int(t, &v)) { day_ymd[1] = v; }
    } else if (k == MESSAGE_KEY_WxDay1High) {
      if (tuple_to_int(t, &v)) { s_wx.day_high_c[1] = (int16_t)v; }
    } else if (k == MESSAGE_KEY_WxDay1Low) {
      if (tuple_to_int(t, &v)) { s_wx.day_low_c[1] = (int16_t)v; }
    } else if (k == MESSAGE_KEY_WxDay1Cond) {
      if (tuple_to_int(t, &v)) { s_wx.day_cond[1] = (uint8_t)v; }
    } else if (k == MESSAGE_KEY_WxDay2Ymd) {
      if (tuple_to_int(t, &v)) { day_ymd[2] = v; }
    } else if (k == MESSAGE_KEY_WxDay2High) {
      if (tuple_to_int(t, &v)) { s_wx.day_high_c[2] = (int16_t)v; }
    } else if (k == MESSAGE_KEY_WxDay2Low) {
      if (tuple_to_int(t, &v)) { s_wx.day_low_c[2] = (int16_t)v; }
    } else if (k == MESSAGE_KEY_WxDay2Cond) {
      if (tuple_to_int(t, &v)) { s_wx.day_cond[2] = (uint8_t)v; }

    } else if (k == MESSAGE_KEY_AccentColor) {
      if (t->type == TUPLE_BYTE_ARRAY && t->length >= 3) {
        // Clay can hand a colour over as its raw components rather than a
        // number; take the last three bytes so both RGB and ARGB orderings
        // land on the same colour.
        const uint8_t *d = t->value->data + (t->length - 3);
        s_settings.accent = ((uint32_t)d[0] << 16) | ((uint32_t)d[1] << 8) | d[2];
        settings_changed = true;
      } else if (tuple_to_int(t, &v)) {
        s_settings.accent = (uint32_t)v & 0xFFFFFF;
        settings_changed = true;
      }
    }
  }

  // A day is only adopted once its date has arrived with it. The date is what
  // makes the payload safe to keep: without it, a forecast fetched yesterday
  // would be indistinguishable from one fetched this morning.
  for (int i = 0; i < WEATHER_DAYS; i++) {
    if (day_ymd[i] > 0) {
      s_wx.day_ymd[i] = day_ymd[i];
      s_wx.have_days |= (uint8_t)(1u << i);
      weather_changed = true;
    }
  }
  if (weather_changed) {
    // Stamped on arrival rather than at the phone: staleness is about how long
    // ago we heard, and the watch's own clock is the one the wearer is reading.
    s_wx.fetched_at = (int32_t)time(NULL);
    save_weather();
    // The answer we were chasing. Note that this fires for an unprompted push
    // too -- the phone sends one when its JavaScript starts -- which is right:
    // the point of the chase is to have current weather, not to have been the
    // one who asked for it.
    wx_schedule_retry(0);
  }

  // Both coordinates must be present and in range before either is adopted, so
  // a partial or malformed message cannot pair a new latitude with a stale
  // longitude.
  bool location_changed = false;
  if (have_lat && have_lon) {
    double lat = lat_raw / 1000000.0;
    double lon = lon_raw / 1000000.0;
    if (coords_sane(lat, lon)) {
      s_lat = lat;
      s_lon = lon;
      location_changed = true;
    }
  }

  if (location_changed) {
    s_have_location = true;
    s_br.valid = false;  // recompute the bracket from the new coordinates
    s_last_day = -1;
    s_next_stale = 0;    // and the next-event cache, which is also per-location
    save_location();
  }
  if (settings_changed && memcmp(&before, &s_settings, sizeof(Settings)) == 0) {
    settings_changed = false;
  }
  if (settings_changed) {
    apply_settings();
    subscribe_tick();
    // Turning a weather slot on is exactly when the wearer expects weather, and
    // it is otherwise up to an hour until the next scheduled request. This
    // cannot loop: a weather payload carries no settings keys, so it cannot set
    // settings_changed and ask again.
    request_weather();
    save_settings();
  }
  // Do the recompute here, in the message callback, rather than leaving it for
  // the next frame to discover: a settings or location change invalidates the
  // bracket, and the render path is the one place this must not happen.
  if (location_changed || settings_changed) refresh(time(NULL));
  if (s_canvas) layer_mark_dirty(s_canvas);
}

static void window_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  s_canvas = layer_create(layer_get_bounds(root));
  layer_set_update_proc(s_canvas, canvas_update);
  layer_add_child(root, s_canvas);
}

static void window_unload(Window *window) {
  layer_destroy(s_canvas);
}

static void init(void) {
  s_bg = GColorFromRGB(16, 16, 18);
  s_fg = GColorWhite;
  s_dim = GColorFromRGB(140, 140, 145);
  s_rule = GColorFromRGB(60, 60, 64);

  load_persisted();

  s_font_shaot = fonts_get_system_font(FONT_KEY_LECO_42_NUMBERS);
  s_font_bold24 = fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD);
  s_font_bold18 = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
  s_font_bold14 = fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD);
  s_font_label = fonts_get_system_font(FONT_KEY_GOTHIC_14);
  s_font_heb24 = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_HEBREW_24));
  s_font_heb18 = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_HEBREW_18));
  s_font_heb14 = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_HEBREW_14));
  apply_settings();

  BatteryChargeState batt = battery_state_service_peek();
  s_battery = batt.charge_percent;
  s_charging = batt.is_charging;

  // Populate the cache before the first frame, while still on main()'s stack.
  // A persisted location means there is real work to do here.
  refresh(time(NULL));

  s_window = window_create();
  window_set_background_color(s_window, s_bg);
  window_set_window_handlers(s_window, (WindowHandlers){
                                           .load = window_load,
                                           .unload = window_unload,
                                       });
  window_stack_push(s_window, true);

  subscribe_tick();
  battery_state_service_subscribe(battery_handler);
  accel_tap_service_subscribe(accel_tap_handler);
  connection_service_subscribe((ConnectionHandlers){
      .pebble_app_connection_handler = connection_handler,
  });

  // Spread the hourly weather fetch across the hour without pulling in rand():
  // two watches have to have been launched in the same second of the same
  // minute to collide, and nothing breaks if they do.
  s_wx_minute = (uint8_t)(time(NULL) % 60);
  s_bt_connected = connection_service_peek_pebble_app_connection();

  // Callbacks must be registered before opening. The inbox has to hold eleven
  // weather keys arriving together, which does not fit the old 256.
  app_message_register_inbox_received(inbox_received);
  app_message_open(512, 64);

  request_weather();
}

static void deinit(void) {
  save_weather();
  wx_schedule_retry(0);
  connection_service_unsubscribe();
  accel_tap_service_unsubscribe();
  battery_state_service_unsubscribe();
  tick_timer_service_unsubscribe();
  for (int i = 0; i < WCOND_COUNT; i++) {
    if (s_wx_icon[i]) gdraw_command_image_destroy(s_wx_icon[i]);
  }
  window_destroy(s_window);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
