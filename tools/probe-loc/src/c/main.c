// Location probe for the Shaot Zemaniot watchface.
//
// The face shows no coordinates and the watch returns no logs, so there is no
// way to tell, from the face itself, whether a position update has landed --
// only that the sunset time changed, which is the same evidence as a new day.
// This app makes the location path visible:
//
//   * the coordinates the watch is currently computing from
//   * how long ago they arrived, and how many messages have arrived in all
//   * a persisted log of the last few, each with how far it moved
//   * how old each fix already was when the phone handed it over, which is the
//     only way to see whether the 30-minute maximumAge is earning anything
//   * the next sunset those coordinates produce, so a reading here can be held
//     against the same box on the face
//
// The distance is computed by the shipping weather.c, not by a copy, so the
// number on screen is the number the face's own predicate tests.
//
// The log is persisted, so swapping to another face and back does not lose it
// -- which matters, because a face swap is also the way to force a fresh fix,
// and the whole question is what changed when it did.
//
// Its own UUID and displayName; install it alongside the watchface, not over
// it. Wearing it means not wearing the face: there is one phone-side runtime
// per watchapp, so the face's location path is stopped while this runs.

#include <pebble.h>

#include "solar.h"
#include "weather.h"

#define LOG_N 6

#define PERSIST_KEY_LAT 1
#define PERSIST_KEY_LON 2
#define PERSIST_KEY_LOG 3

typedef struct {
  int32_t at;      // unix time the message arrived
  int32_t dist_m;  // metres from the previous coordinates; 0 if unchanged
  int32_t age_s;   // how old the fix was when handed over; -1 if not from a fix
  uint8_t first;   // the first fix of all, which is not a move
  uint8_t pad[3];
} LocEvent;

// Every counter the log cannot reconstruct once it has wrapped.
typedef struct {
  uint16_t msgs;     // LAT/LON messages received, unchanged ones included
  uint16_t changes;  // those that actually moved the coordinates
  uint16_t n;        // entries written, capped at LOG_N
  uint16_t head;     // next slot to write
  LocEvent ev[LOG_N];
} LocLog;

static Window *s_window;
static Layer *s_canvas;

static int32_t s_lat_micro;
static int32_t s_lon_micro;
static bool s_have;
static LocLog s_log;

// The minute of the hour this watch asks on, chosen once per launch, exactly as
// the face does it -- a fixed minute would have every watch running this hit the
// same tick.
static int s_ask_minute;

// --- persistence ------------------------------------------------------------

static void load_persisted(void) {
  if (persist_exists(PERSIST_KEY_LAT) && persist_exists(PERSIST_KEY_LON)) {
    s_lat_micro = persist_read_int(PERSIST_KEY_LAT);
    s_lon_micro = persist_read_int(PERSIST_KEY_LON);
    s_have = true;
  }
  // Same size check the face uses on its settings, for the same reason: a build
  // that changed this layout would otherwise read an old blob as the new one.
  if (persist_exists(PERSIST_KEY_LOG) &&
      persist_get_size(PERSIST_KEY_LOG) == (int)sizeof(LocLog)) {
    persist_read_data(PERSIST_KEY_LOG, &s_log, sizeof(LocLog));
  }
}

static void save_persisted(void) {
  persist_write_int(PERSIST_KEY_LAT, s_lat_micro);
  persist_write_int(PERSIST_KEY_LON, s_lon_micro);
  persist_write_data(PERSIST_KEY_LOG, &s_log, sizeof(LocLog));
}

static void log_event(int32_t dist_m, int32_t age_s, bool first) {
  LocEvent *e = &s_log.ev[s_log.head];
  e->at = (int32_t)time(NULL);
  e->dist_m = dist_m;
  e->age_s = age_s;
  e->first = first ? 1 : 0;
  s_log.head = (uint16_t)((s_log.head + 1) % LOG_N);
  if (s_log.n < LOG_N) s_log.n++;
}

// --- formatting -------------------------------------------------------------
//
// All integer. The SDK's snprintf has no %f, so every number here is split into
// its parts by hand rather than handed to a format string as a double.

static void fmt_deg(char *out, size_t n, int32_t micro) {
  const char *sign = (micro < 0) ? "-" : "";
  int32_t a = (micro < 0) ? -micro : micro;
  snprintf(out, n, "%s%ld.%04ld", sign, (long)(a / 1000000), (long)((a % 1000000) / 100));
}

static void fmt_km(char *out, size_t n, int32_t metres) {
  snprintf(out, n, "%ld.%01ldkm", (long)(metres / 1000), (long)((metres % 1000) / 100));
}

// Elapsed time, in the largest unit that still says something useful.
static void fmt_ago(char *out, size_t n, int32_t secs) {
  if (secs < 0) secs = 0;
  if (secs < 3600) {
    snprintf(out, n, "%ldm%02lds", (long)(secs / 60), (long)(secs % 60));
  } else {
    snprintf(out, n, "%ldh%02ldm", (long)(secs / 3600), (long)((secs % 3600) / 60));
  }
}

static void fmt_clock(char *out, size_t n, time_t t) {
  struct tm *lt = localtime(&t);
  if (!lt) {
    snprintf(out, n, "--:--");
    return;
  }
  snprintf(out, n, "%02d:%02d", lt->tm_hour, lt->tm_min);
}

// --- drawing ----------------------------------------------------------------

static void line(GContext *ctx, const char *text, int y, int w, bool bold) {
  graphics_draw_text(ctx, text,
                     fonts_get_system_font(bold ? FONT_KEY_GOTHIC_18_BOLD : FONT_KEY_GOTHIC_14),
                     GRect(2, y, w - 4, 22), GTextOverflowModeTrailingEllipsis,
                     GTextAlignmentLeft, NULL);
}

static void canvas_update(Layer *layer, GContext *ctx) {
  const GRect b = layer_get_bounds(layer);
  const int w = b.size.w;
  const time_t now = time(NULL);
  char buf[48], a[16], c[16];

  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, b, 0, GCornerNone);
  graphics_context_set_text_color(ctx, GColorWhite);

  int y = 0;
  line(ctx, "LOCATION PROBE", y, w, true);
  y += 22;

  if (!s_have) {
    line(ctx, "no fix yet", y, w, false);
    return;
  }

  fmt_deg(a, sizeof(a), s_lat_micro);
  fmt_deg(c, sizeof(c), s_lon_micro);
  snprintf(buf, sizeof(buf), "%s  %s", a, c);
  line(ctx, buf, y, w, false);
  y += 16;

  // The newest entry is one before the write head.
  if (s_log.n > 0) {
    const LocEvent *last = &s_log.ev[(s_log.head + LOG_N - 1) % LOG_N];
    fmt_ago(a, sizeof(a), (int32_t)now - last->at);
    snprintf(buf, sizeof(buf), "last msg %s ago", a);
    line(ctx, buf, y, w, false);
  }
  y += 16;

  snprintf(buf, sizeof(buf), "msgs %d  moved %d", (int)s_log.msgs,
           (int)s_log.changes);
  line(ctx, buf, y, w, false);
  y += 16;

  // The face's own reading, from these coordinates. Held against the sunset box
  // on the watchface, a difference means the two are on different positions.
  {
    double v;
    const double lat = s_lat_micro / 1000000.0;
    const double lon = s_lon_micro / 1000000.0;
    if (solar_next_event((double)now * 1000.0, lat, lon, SUNRISE_SET_ANGLE, false, &v)) {
      fmt_clock(c, sizeof(c), (time_t)(v / 1000.0));
      snprintf(buf, sizeof(buf), "next sunset %s", c);
    } else {
      snprintf(buf, sizeof(buf), "next sunset --:--");
    }
    line(ctx, buf, y, w, false);
  }
  y += 20;

  line(ctx, "LOG, NEWEST FIRST", y, w, true);
  y += 20;

  for (int i = 0; i < s_log.n; i++) {
    const LocEvent *e = &s_log.ev[(s_log.head + LOG_N - 1 - i + LOG_N) % LOG_N];
    char age[12];
    fmt_clock(c, sizeof(c), (time_t)e->at);
    // "~" because it is how stale the fix already was when it arrived, not how
    // long ago it arrived. A high number here with the phone in a pocket means
    // the 30-minute allowance is buying lag rather than saving a receiver.
    if (e->age_s < 0) {
      age[0] = '\0';  // cached coordinates, with no fix behind them
    } else {
      snprintf(age, sizeof(age), "  ~%ldm", (long)(e->age_s / 60));
    }
    if (e->first) {
      snprintf(buf, sizeof(buf), "%s  first fix%s", c, age);
    } else if (e->dist_m == 0) {
      // A message that changed nothing is the useful negative: it says the
      // top-up ran and the wearer had not moved, which is a different fact from
      // no message at all.
      snprintf(buf, sizeof(buf), "%s  same%s", c, age);
    } else {
      fmt_km(a, sizeof(a), e->dist_m);
      snprintf(buf, sizeof(buf), "%s  +%s%s", c, a, age);
    }
    line(ctx, buf, y, w, false);
    y += 16;
  }
}

// --- messages ---------------------------------------------------------------

// The same hourly wake the face sends, with the same key. WantWx is always 0
// here: this app displays no weather, so there is nothing for the phone to
// fetch -- but the position top-up on the far side is ungated, which is exactly
// the behaviour being probed.
static void send_request(void) {
  DictionaryIterator *iter;
  if (app_message_outbox_begin(&iter) != APP_MSG_OK) return;
  dict_write_uint8(iter, MESSAGE_KEY_WantWx, 0);
  app_message_outbox_send();
}

// A probe that silently ignored a message would be worse than no probe. These
// cost nothing on hardware, where there is no one to read them, and are the
// difference between "nothing arrived" and "something arrived and was thrown
// away" when running under the emulator.
static void inbox_dropped(AppMessageResult reason, void *context) {
  APP_LOG(APP_LOG_LEVEL_ERROR, "inbox dropped: 0x%02x", (unsigned)reason);
}

static void outbox_failed(DictionaryIterator *iter, AppMessageResult reason, void *context) {
  APP_LOG(APP_LOG_LEVEL_ERROR, "outbox failed: 0x%02x", (unsigned)reason);
}

static void inbox_received(DictionaryIterator *iter, void *context) {
  bool have_lat = false, have_lon = false;
  int32_t lat_raw = 0, lon_raw = 0, age_s = -1;

  for (Tuple *t = dict_read_first(iter); t; t = dict_read_next(iter)) {
    if (t->key == MESSAGE_KEY_LAT) {
      lat_raw = t->value->int32;
      have_lat = true;
    } else if (t->key == MESSAGE_KEY_LON) {
      lon_raw = t->value->int32;
      have_lon = true;
    } else if (t->key == MESSAGE_KEY_FixAge) {
      age_s = t->value->int32;
    }
  }
  if (!have_lat || !have_lon) {
    APP_LOG(APP_LOG_LEVEL_WARNING, "message with no coordinates");
    return;
  }

  APP_LOG(APP_LOG_LEVEL_INFO, "loc %ld %ld", (long)lat_raw, (long)lon_raw);
  s_log.msgs++;

  const bool first = !s_have;
  int32_t dist_m = 0;
  if (!first && (lat_raw != s_lat_micro || lon_raw != s_lon_micro)) {
    const double lat1 = s_lat_micro / 1000000.0, lon1 = s_lon_micro / 1000000.0;
    const double lat2 = lat_raw / 1000000.0, lon2 = lon_raw / 1000000.0;
    dist_m = (int32_t)(weather_move_km(lat1, lon1, lat2, lon2) * 1000.0);
    s_log.changes++;
  }

  s_lat_micro = lat_raw;
  s_lon_micro = lon_raw;
  s_have = true;
  log_event(dist_m, age_s, first);
  save_persisted();
  if (s_canvas) layer_mark_dirty(s_canvas);
}

// --- services ---------------------------------------------------------------

static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  // The same hourly cadence the face wakes the phone on. Asking across a dead
  // link spends a wake to reach nobody.
  if (tick_time->tm_min == s_ask_minute && tick_time->tm_sec == 0 &&
      connection_service_peek_pebble_app_connection()) {
    send_request();
  }
  layer_mark_dirty(s_canvas);
}

static void connection_handler(bool connected) {
  if (connected) send_request();
  layer_mark_dirty(s_canvas);
}

static void window_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  s_canvas = layer_create(layer_get_bounds(root));
  layer_set_update_proc(s_canvas, canvas_update);
  layer_add_child(root, s_canvas);
}

static void window_unload(Window *window) {
  layer_destroy(s_canvas);
  s_canvas = NULL;
}

static void init(void) {
  load_persisted();

  s_window = window_create();
  window_set_window_handlers(s_window, (WindowHandlers){
                                           .load = window_load,
                                           .unload = window_unload,
                                       });
  window_stack_push(s_window, true);

  app_message_register_inbox_received(inbox_received);
  app_message_register_inbox_dropped(inbox_dropped);
  app_message_register_outbox_failed(outbox_failed);
  // The same explicit sizes the watchface opens with, rather than the maxima:
  // this app carries two keys, and matching what is proven on hardware is worth
  // more here than headroom nothing will use.
  app_message_open(512, 64);

  connection_service_subscribe((ConnectionHandlers){
      .pebble_app_connection_handler = connection_handler,
  });

  // Spread across the hour without pulling in rand(), as the face does.
  s_ask_minute = (int)(time(NULL) % 60);

  tick_timer_service_subscribe(SECOND_UNIT, tick_handler);
  send_request();
}

static void deinit(void) {
  tick_timer_service_unsubscribe();
  connection_service_unsubscribe();
  window_destroy(s_window);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
  return 0;
}
