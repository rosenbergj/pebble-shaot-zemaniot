// Integer-only diagnostic probe.
//
// Companion to tools/probe. That one exercises libm and soft-float doubles via
// the solar and calendar maths; this one contains no floating point at all --
// no double, no float, no math.h, and none of the pure modules, since hebdate.c
// uses double and floor() as well as solar.c.
//
// The pair isolates one question: TimeStyle is a native C watchface that runs
// on the same watch and uses no floating point anywhere, while both of our C
// builds crash at launch. If this probe runs and tools/probe does not, the
// difference is floating point. If neither runs, it is something structural in
// how this project is built, and TimeStyle CTRL (the same TimeStyle source
// built with this toolchain) says which.
//
// Nothing here touches AppMessage or persistent storage.

#include <pebble.h>

static Window *s_window;
static Layer *s_canvas;
static int s_battery;
static int s_ticks;

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
  draw_line(ctx, "int only", 8, b.size.w);

  time_t now = time(NULL);
  struct tm *lt = localtime(&now);
  if (lt) {
    snprintf(buf, sizeof(buf), "%d:%02d:%02d", lt->tm_hour, lt->tm_min, lt->tm_sec);
    draw_line(ctx, buf, 48, b.size.w);
  }

  snprintf(buf, sizeof(buf), "batt %d%%", s_battery);
  draw_line(ctx, buf, 88, b.size.w);

  snprintf(buf, sizeof(buf), "ticks %d", s_ticks);
  draw_line(ctx, buf, 128, b.size.w);
}

static void tick_handler(struct tm *t, TimeUnits u) {
  s_ticks++;
  layer_mark_dirty(s_canvas);
}

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
