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
#include "shaot.h"
#include "solar.h"

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
  SLOT_SUNRISE = 6,
  SLOT_NEXT_SET_TZEIT = 7,       // sunset or nightfall
  SLOT_NEXT_RISE_SET = 8,        // sunrise or sunset
  SLOT_NEXT_RISE_SET_TZEIT = 9,  // any of the three
} SlotKind;

typedef struct {
  bool offset6;
  bool with_minutes;
  bool tick_seconds;
  bool hebrew_script;
  uint8_t slot_band, slot_left, slot_mid, slot_right;
  uint32_t accent;      // 0xRRGGBB
  uint8_t civil_font;   // 0 = Roboto 49, 1 = Leco 42 (matches the shaot face)
} Settings;

static Settings s_settings = {
    .offset6 = false,
    .with_minutes = true,
    .tick_seconds = true,
    .hebrew_script = false,
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

#define CIVIL_Y 46

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
static GFont s_font_label;    // Gothic 14
static GFont s_font_civil;    // Roboto 49 or the shaot face
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
static time_t s_sunrise_at;
static time_t s_sunset_at;
static time_t s_tzeit_at;
static bool s_have_sunrise;
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
static int s_battery = 0;

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

// Fills label and value. Returns true when the two lines are a matched pair
// (a date split across both lines) rather than a label above a value.
static bool slot_content(uint8_t kind, const struct tm *lt, bool for_band,
                         char *label, size_t label_n, char *value, size_t value_n) {
  label[0] = '\0';
  value[0] = '\0';

  switch (kind) {
    case SLOT_HEBREW: {
      const char *month = hebdate_month_name(s_heb.year, s_heb.month, s_settings.hebrew_script);
      if (for_band) {
        snprintf(value, value_n, "%d %s", s_heb.day, month);
        return false;
      }
      // In a box, split across both lines ("29th of" / "Av") rather than
      // spending a line on a label: the widest thing is then just the month
      // name, so long ones like Heshvan still fit.
      snprintf(label, label_n, "%d%s of", s_heb.day, ordinal_suffix(s_heb.day));
      snprintf(value, value_n, "%s", month);
      return true;
    }
    case SLOT_SECDATE:
      if (for_band) {
        snprintf(value, value_n, "%s %s %d", WDAYS[lt->tm_wday], GMONTHS[lt->tm_mon], lt->tm_mday);
        return false;
      }
      snprintf(label, label_n, "%s", WDAYS[lt->tm_wday]);
      snprintf(value, value_n, "%s %d", GMONTHS[lt->tm_mon], lt->tm_mday);
      return false;
    case SLOT_SUNRISE:
      snprintf(label, label_n, "sunrise");
      if (s_have_sunrise) format_hhmm(s_sunrise_at, value, value_n);
      else snprintf(value, value_n, "--:--");
      return false;
    case SLOT_SUNSET:
      snprintf(label, label_n, "sunset");
      if (s_have_sunset) format_hhmm(s_sunset_at, value, value_n);
      else snprintf(value, value_n, "--:--");
      return false;
    case SLOT_TZEIT:
      snprintf(label, label_n, "tzeit");
      if (s_have_tzeit) format_hhmm(s_tzeit_at, value, value_n);
      else snprintf(value, value_n, "--:--");
      return false;
    case SLOT_NEXT_SET_TZEIT:
      next_event_content(false, true, true, label, label_n, value, value_n);
      return false;
    case SLOT_NEXT_RISE_SET:
      next_event_content(true, true, false, label, label_n, value, value_n);
      return false;
    case SLOT_NEXT_RISE_SET_TZEIT:
      next_event_content(true, true, true, label, label_n, value, value_n);
      return false;
    case SLOT_BATTERY:
      snprintf(label, label_n, "batt");
      snprintf(value, value_n, "%d%%", s_battery);
      return false;
    default:
      return false;
  }
}

// The band is a single line, so a labelled slot joins its two parts with a
// colon -- "sunset: 7:58" -- where a footer box stacks them. Content that names
// itself, meaning either date, carries no label and is shown as it stands.
static void band_content(uint8_t kind, const struct tm *lt, char *out, size_t out_n) {
  char label[24], value[24];
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

static GSize measure(const char *text, GFont font) {
  return graphics_text_layout_get_content_size(text, font, GRect(0, 0, 200, 60),
                                               GTextOverflowModeTrailingEllipsis,
                                               GTextAlignmentLeft);
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
  s_have_sunrise = false;
  s_have_sunset = false;
  s_have_tzeit = false;

  double midnight_ms = (double)time_start_of_today() * 1000.0;
  double sunrise_ms, sunset_ms, tzeit_ms;
  if (solar_next_event(midnight_ms, s_lat, s_lon, SUNRISE_SET_ANGLE, true, &sunrise_ms)) {
    s_sunrise_at = (time_t)(sunrise_ms / 1000.0);
    s_have_sunrise = true;
  }
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
static void refresh(time_t now) {
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

  // The combined slots expire on their own schedule -- when the event they are
  // showing happens -- rather than at midnight, and cost three more passes of
  // the solar maths, so they are only computed while one is configured.
  if (any_next_slot() && (now >= s_next_stale || now < s_next_from)) update_next_events(now);
}

static void canvas_update(Layer *layer, GContext *ctx) {
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

  char label[24], value[24], band[51];  // both parts plus ": " and the NUL
  band_content(s_settings.slot_band, &lt, band, sizeof(band));
  draw_centered(ctx, band, s_font_bold24, LEAD_GOTHIC24, s_on_accent, 5, 0, bounds.size.w);

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

  // Shaot. Ticking once a minute the reading would otherwise be up to a whole
  // minute stale, always in the same direction; half a minute ahead makes it
  // right on average across the minute it sits unchanged.
  char shaot[16];
  double shaot_ms = (double)now * 1000.0;
  if (!s_settings.tick_seconds) shaot_ms += 30000.0;
  shaot_format(shaot_chalakim_now(shaot_ms, s_br.start_ms, s_br.end_ms),
               s_settings.offset6, s_settings.with_minutes, shaot, sizeof(shaot));
  draw_centered(ctx, shaot, s_font_shaot, LEAD_LECO42, s_fg, 112, 0, bounds.size.w);

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
  graphics_context_set_fill_color(ctx, s_rule);
  graphics_fill_rect(ctx, GRect(0, footer_top, bounds.size.w, 1), 0, GCornerNone);
  graphics_context_set_fill_color(ctx, s_accent);
  graphics_fill_rect(ctx, GRect(0, footer_top + 1, third + 1, footer_h), 0, GCornerNone);
  graphics_fill_rect(ctx, GRect(2 * third, footer_top + 1, bounds.size.w - 2 * third, footer_h),
                     0, GCornerNone);

  const uint8_t kinds[3] = {s_settings.slot_left, s_settings.slot_mid, s_settings.slot_right};
  for (int i = 0; i < 3; i++) {
    bool on_fill = (i != 1);
    GColor ink = on_fill ? s_on_accent : s_fg;
    int x = i * third;
    int w = (i == 2) ? bounds.size.w - 2 * third : third + 1;

    bool split = slot_content(kinds[i], &lt, false, label, sizeof(label), value, sizeof(value));
    if (split) {
      // A date split over both lines: same size and weight, no label.
      draw_centered(ctx, label, s_font_bold24, LEAD_GOTHIC24, ink, footer_top + 3, x, w);
      draw_centered(ctx, value, s_font_bold24, LEAD_GOTHIC24, ink, footer_top + 29, x, w);
    } else {
      draw_centered(ctx, label, s_font_label, LEAD_GOTHIC14, on_fill ? s_on_accent : s_dim,
                    footer_top + 6, x, w);
      draw_centered(ctx, value, s_font_bold24, LEAD_GOTHIC24, ink, footer_top + 24, x, w);
    }
  }
}

// --- services ---------------------------------------------------------------

static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  // Recompute here rather than while drawing. refresh() is cheap on the vast
  // majority of ticks: it only does real work when the bracket has run out or
  // the day has rolled.
  refresh(time(NULL));
  layer_mark_dirty(s_canvas);
}

static void battery_handler(BatteryChargeState state) {
  s_battery = state.charge_percent;
  layer_mark_dirty(s_canvas);
}

static void subscribe_tick(void) {
  // SECOND_UNIT is deliberate: per-second chalakim is the point of this face,
  // and the tick rate is a user setting. Do not "optimise" this to MINUTE_UNIT.
  tick_timer_service_subscribe(s_settings.tick_seconds ? SECOND_UNIT : MINUTE_UNIT,
                               tick_handler);
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
  bool settings_changed = false;
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
  if (settings_changed) {
    apply_settings();
    subscribe_tick();
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
  s_font_label = fonts_get_system_font(FONT_KEY_GOTHIC_14);
  apply_settings();

  s_battery = battery_state_service_peek().charge_percent;

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

  // Callbacks must be registered before opening.
  app_message_register_inbox_received(inbox_received);
  app_message_open(256, 64);
}

static void deinit(void) {
  battery_state_service_unsubscribe();
  tick_timer_service_unsubscribe();
  window_destroy(s_window);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
