// Diagnostic probe for the Shaot Zemaniot C port.
//
// The watchface itself shows "not responding" on real hardware while working
// in the emulator, and there is no way to read logs off the watch. This app
// exists to narrow that down: it draws one line per subsystem, in order, so
// whatever appears on screen says how far execution got.
//
//   line 1  "stage N"     -- how many stages completed before the last draw
//   line 2  civil time    -- native C, fonts, text drawing, tick service
//   line 3  sunset        -- libm, soft-float doubles, the NOAA solar math
//   line 4  Hebrew date   -- the integer calendar math
//   line 5  battery       -- the real battery service (the emulator fakes 100%)
//
// A blank or missing line is the answer: the stage counter shows where it
// stopped. Nothing here touches AppMessage or persistent storage, so those are
// deliberately excluded from what this can blame.

#include <pebble.h>

#include "hebdate.h"
#include "shaot.h"
#include "solar.h"

// Coarse coordinates, deliberately not anyone's exact position.
#define PROBE_LAT 39.95
#define PROBE_LON (-75.17)

static Window *s_window;
static Layer *s_canvas;
static int s_stage;
static int s_battery;

static void draw_line(GContext *ctx, const char *text, int y, int w) {
  graphics_draw_text(ctx, text, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD),
                     GRect(0, y, w, 32), GTextOverflowModeTrailingEllipsis,
                     GTextAlignmentCenter, NULL);
}

static void canvas_update(Layer *layer, GContext *ctx) {
  GRect b = layer_get_bounds(layer);
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, b, 0, GCornerNone);
  graphics_context_set_text_color(ctx, GColorWhite);

  char buf[32];
  int stage = 0;

  // Stage 1: the C app is running and can draw text.
  time_t now = time(NULL);
  struct tm *lt = localtime(&now);
  stage = 1;
  if (lt) {
    snprintf(buf, sizeof(buf), "%d:%02d:%02d", lt->tm_hour, lt->tm_min, lt->tm_sec);
    draw_line(ctx, buf, 40, b.size.w);
    stage = 2;
  }

  // Stage 2: libm and the solar math in soft-float double.
  double now_ms = (double)now * 1000.0;
  SolarBracket br = solar_bracket(now_ms, PROBE_LAT, PROBE_LON);
  stage = 3;
  if (br.valid) {
    double sunset_ms = br.is_day ? br.end_ms : br.start_ms;
    time_t st = (time_t)(sunset_ms / 1000.0);
    struct tm *slt = localtime(&st);
    if (slt) {
      int h = slt->tm_hour % 12;
      if (h == 0) h = 12;
      snprintf(buf, sizeof(buf), "sunset %d:%02d", h, slt->tm_min);
    } else {
      snprintf(buf, sizeof(buf), "sunset ??");
    }
    draw_line(ctx, buf, 72, b.size.w);
    stage = 4;
  }

  // Stage 3: the integer calendar math.
  if (lt) {
    HebrewDate hd = hebdate_for_now(lt->tm_year + 1900, lt->tm_mon + 1, lt->tm_mday,
                                    lt->tm_hour, br.valid ? br.is_day : true);
    snprintf(buf, sizeof(buf), "%d %s", hd.day, hebdate_month_name(hd.year, hd.month, false));
    draw_line(ctx, buf, 104, b.size.w);
    stage = 5;
  }

  // Stage 4: the real battery service.
  snprintf(buf, sizeof(buf), "batt %d%%", s_battery);
  draw_line(ctx, buf, 136, b.size.w);
  stage = 6;

  // Drawn last so it reports the previous pass: if a stage faults, the count
  // from the pass before it is already on screen.
  snprintf(buf, sizeof(buf), "stage %d", s_stage);
  draw_line(ctx, buf, 8, b.size.w);
  s_stage = stage;
}

static void tick_handler(struct tm *t, TimeUnits u) { layer_mark_dirty(s_canvas); }

static void battery_handler(BatteryChargeState st) {
  s_battery = st.charge_percent;
  layer_mark_dirty(s_canvas);
}

static void window_load(Window *w) {
  Layer *root = window_get_root_layer(w);
  s_canvas = layer_create(layer_get_bounds(root));
  layer_set_update_proc(s_canvas, canvas_update);
  layer_add_child(root, s_canvas);
}

static void window_unload(Window *w) { layer_destroy(s_canvas); }

int main(void) {
  s_battery = battery_state_service_peek().charge_percent;
  s_window = window_create();
  window_set_background_color(s_window, GColorBlack);
  window_set_window_handlers(s_window, (WindowHandlers){
                                           .load = window_load,
                                           .unload = window_unload,
                                       });
  window_stack_push(s_window, true);
  tick_timer_service_subscribe(SECOND_UNIT, tick_handler);
  battery_state_service_subscribe(battery_handler);
  app_event_loop();
  tick_timer_service_unsubscribe();
  battery_state_service_unsubscribe();
  window_destroy(s_window);
}
