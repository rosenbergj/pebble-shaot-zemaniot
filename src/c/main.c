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

typedef enum {
  SLOT_NONE = 0,
  SLOT_HEBREW = 1,
  SLOT_SECDATE = 2,
  SLOT_SUNSET = 3,
  SLOT_TZEIT = 4,
  SLOT_BATTERY = 5,
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
#define FOOTER_TOP 170

// graphics_draw_text() positions glyphs below the top of its box by a
// font-specific internal leading, which Poco did not add. The y values below
// are the original Alloy coordinates, so each draw subtracts its font's
// leading to land in the same place. Measured from emulator screenshots by
// comparing glyph bounding boxes against the JavaScript build.
#define LEAD_GOTHIC14 2
#define LEAD_GOTHIC24 4
#define LEAD_LECO42 8
#define LEAD_ROBOTO49 9

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
static char s_sunset[8] = "--:--";
static char s_tzeit[8] = "--:--";
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

// 12-hour clock without a meridiem suffix, matching the JavaScript build.
// localtime() returns NULL for a time_t it cannot break down, so a bad solar
// result must not be dereferenced -- that is a hard fault on the watch, where
// there is no console to see it happen.
static void format_hhmm(time_t t, char *out, size_t n) {
  struct tm *lt = localtime(&t);
  if (!lt) {
    snprintf(out, n, "--:--");
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
    case SLOT_SUNSET:
      snprintf(label, label_n, "sunset");
      snprintf(value, value_n, "%s", s_sunset);
      return false;
    case SLOT_TZEIT:
      snprintf(label, label_n, "tzeit");
      snprintf(value, value_n, "%s", s_tzeit);
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
    s_last_day = -1;  // a bracket flip can roll the Hebrew date
    if (s_br.valid) {
      double sunset_ms = s_br.is_day ? s_br.end_ms : s_br.start_ms;
      format_hhmm((time_t)(sunset_ms / 1000.0), s_sunset, sizeof(s_sunset));
      double tz;
      if (solar_next_event(sunset_ms, s_lat, s_lon, TZEIT_ANGLE, false, &tz)) {
        format_hhmm((time_t)(tz / 1000.0), s_tzeit, sizeof(s_tzeit));
      } else {
        snprintf(s_tzeit, sizeof(s_tzeit), "--:--");
      }
    }
  }

  struct tm *lt = localtime(&now);
  if (s_br.valid && lt->tm_mday != s_last_day) {
    s_last_day = lt->tm_mday;
    s_heb = hebdate_for_now(lt->tm_year + 1900, lt->tm_mon + 1, lt->tm_mday,
                            lt->tm_hour, s_br.is_day);
  }
}

static void canvas_update(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  time_t now = time(NULL);
  // Draw only. See refresh(): the solar maths must not run from here.

  graphics_context_set_fill_color(ctx, s_bg);
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);

  if (!s_have_location || !s_br.valid) {
    // Say what is wrong rather than showing a stale or invented time. No
    // location means the phone has not reported one yet; a location with no
    // bracket means a polar latitude where the sun does not cross today.
    draw_centered(ctx, s_have_location ? "no sun window" : "waiting for phone",
                  s_font_bold24, LEAD_GOTHIC24, s_dim, 100, 0, bounds.size.w);
    return;
  }

  struct tm *lt = localtime(&now);

  // Band
  graphics_context_set_fill_color(ctx, s_accent);
  graphics_fill_rect(ctx, GRect(0, 0, bounds.size.w, BAND_H), 0, GCornerNone);

  char label[24], value[24], band[51];  // both parts plus ": " and the NUL
  band_content(s_settings.slot_band, lt, band, sizeof(band));
  draw_centered(ctx, band, s_font_bold24, LEAD_GOTHIC24, s_on_accent, 5, 0, bounds.size.w);

  // Civil time
  char civil[16];
  int h12 = lt->tm_hour % 12;
  if (h12 == 0) h12 = 12;
  snprintf(civil, sizeof(civil), "%d:%02d:%02d", h12, lt->tm_min, lt->tm_sec);
  draw_centered(ctx, civil, s_font_civil, s_lead_civil, s_fg, 46, 0, bounds.size.w);

  // Shaot
  char shaot[16];
  shaot_format(shaot_chalakim_now((double)now * 1000.0, s_br.start_ms, s_br.end_ms),
               s_settings.offset6, s_settings.with_minutes, shaot, sizeof(shaot));
  draw_centered(ctx, shaot, s_font_shaot, LEAD_LECO42, s_fg, 112, 0, bounds.size.w);

  // Footer. The accent fill belongs to the outer slot positions, not to their
  // content: every slot is user-configurable, only the fill colour is.
  int footer_h = bounds.size.h - FOOTER_TOP - 1;
  int third = bounds.size.w / 3;
  graphics_context_set_fill_color(ctx, s_rule);
  graphics_fill_rect(ctx, GRect(0, FOOTER_TOP, bounds.size.w, 1), 0, GCornerNone);
  graphics_context_set_fill_color(ctx, s_accent);
  graphics_fill_rect(ctx, GRect(0, FOOTER_TOP + 1, third + 1, footer_h), 0, GCornerNone);
  graphics_fill_rect(ctx, GRect(2 * third, FOOTER_TOP + 1, bounds.size.w - 2 * third, footer_h),
                     0, GCornerNone);

  const uint8_t kinds[3] = {s_settings.slot_left, s_settings.slot_mid, s_settings.slot_right};
  for (int i = 0; i < 3; i++) {
    bool on_fill = (i != 1);
    GColor ink = on_fill ? s_on_accent : s_fg;
    int x = i * third;
    int w = (i == 2) ? bounds.size.w - 2 * third : third + 1;

    bool split = slot_content(kinds[i], lt, false, label, sizeof(label), value, sizeof(value));
    if (split) {
      // A date split over both lines: same size and weight, no label.
      draw_centered(ctx, label, s_font_bold24, LEAD_GOTHIC24, ink, FOOTER_TOP + 3, x, w);
      draw_centered(ctx, value, s_font_bold24, LEAD_GOTHIC24, ink, FOOTER_TOP + 29, x, w);
    } else {
      draw_centered(ctx, label, s_font_label, LEAD_GOTHIC14, on_fill ? s_on_accent : s_dim,
                    FOOTER_TOP + 6, x, w);
      draw_centered(ctx, value, s_font_bold24, LEAD_GOTHIC24, ink, FOOTER_TOP + 24, x, w);
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
