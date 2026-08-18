// Touch probe: does a *watchface* receive screen touch on Emery hardware?
//
// The SDK headers place no restriction on touch by process type, but they also
// never say a watchface gets it, and the "watchfaces cannot use the buttons"
// note predates touch hardware entirely. Firmware is where any gate would live,
// and we cannot read it. There is no emu-touch in the pebble tool and no touch
// endpoint in the protocol, so this cannot be settled in the emulator either.
// Hence: a throwaway watchface that reports what it receives, read off the
// watch by eye.
//
// Three things could each independently be the thing that does not work, so the
// probe cycles through them rather than running them together -- with all three
// live at once, silence would not say which one was at fault:
//
//   RAW      touch_service_subscribe(), system touch bridge left alone
//   RAW-NB   touch_service_subscribe(), touch bridge disabled
//   RECOG    tap_recognizer_create() attached to the window, bridge disabled
//
// The bridge matters because window_set_touch_bridge_disabled() documents that
// the system recognizer set otherwise fails on Touchdown for the window, which
// leaves open whether it swallows the raw stream too.
//
// The mode is cycled by an accelerometer tap, which is both the one input we
// are confident a watchface receives and the control for this experiment: if
// the mode never changes, the probe is not running and nothing else it shows
// means anything.

#include <pebble.h>

#define PERSIST_KEY_STATE 1

typedef enum {
  MODE_RAW = 0,
  MODE_RAW_NOBRIDGE = 1,
  MODE_RECOG = 2,
  MODE_COUNT = 3,
} ProbeMode;

// Counters survive a relaunch on purpose. If touching the screen bounces us out
// to the launcher, the watchface reloads with everything zeroed, and a zero
// would read as "no touch received" when in fact touch was received and then
// acted on by the system. Persisted counters tell those two apart.
typedef struct {
  uint8_t mode;
  uint8_t nonnav;  // 0 = nothing yet, 1 = navigational, 2 = non-navigational
  int16_t last_x;
  int16_t last_y;
  uint32_t n_down;
  uint32_t n_move;
  uint32_t n_up;
  uint32_t n_recog;
  uint32_t n_accel;
  uint32_t n_launch;
} ProbeState;

static ProbeState s_state;
static Window *s_window;
static Layer *s_layer;
static Recognizer *s_tap_recognizer;
static bool s_recognizer_attached;
static bool s_touch_subscribed;
static bool s_touch_enabled;

static void state_save(void) {
  persist_write_data(PERSIST_KEY_STATE, &s_state, sizeof(s_state));
}

static void state_load(void) {
  if (persist_exists(PERSIST_KEY_STATE) &&
      persist_get_size(PERSIST_KEY_STATE) == (int)sizeof(ProbeState)) {
    persist_read_data(PERSIST_KEY_STATE, &s_state, sizeof(s_state));
  }
  s_state.n_launch++;
}

static void touch_handler(const TouchEvent *event, void *context) {
  switch (event->type) {
    case TouchEvent_Touchdown: s_state.n_down++; break;
    case TouchEvent_PositionUpdate: s_state.n_move++; break;
    case TouchEvent_Liftoff: s_state.n_up++; break;
  }
  s_state.last_x = event->x;
  s_state.last_y = event->y;
  s_state.nonnav = event->non_navigational ? 2 : 1;
  layer_mark_dirty(s_layer);
}

static void recognizer_handler(const Recognizer *recognizer, RecognizerEvent event_type) {
  if (event_type != RecognizerEvent_Completed) return;
  const GPoint p = tap_recognizer_get_tap_point(recognizer);
  s_state.n_recog++;
  s_state.last_x = p.x;
  s_state.last_y = p.y;
  layer_mark_dirty(s_layer);
}

// Tear down whichever mechanism was live and stand up the one the mode names.
// Both are torn down first so a mode never inherits a subscription from the one
// before it.
static void apply_mode(void) {
  if (s_touch_subscribed) {
    touch_service_unsubscribe();
    s_touch_subscribed = false;
  }
  if (s_recognizer_attached) {
    window_detach_recognizer(s_window, s_tap_recognizer);
    s_recognizer_attached = false;
  }

  switch (s_state.mode) {
    case MODE_RAW:
      window_set_touch_bridge_disabled(s_window, false);
      touch_service_subscribe(touch_handler, NULL);
      s_touch_subscribed = true;
      break;
    case MODE_RAW_NOBRIDGE:
      window_set_touch_bridge_disabled(s_window, true);
      touch_service_subscribe(touch_handler, NULL);
      s_touch_subscribed = true;
      break;
    case MODE_RECOG:
      window_set_touch_bridge_disabled(s_window, true);
      window_attach_recognizer(s_window, s_tap_recognizer);
      s_recognizer_attached = true;
      break;
  }

  s_touch_enabled = touch_service_is_enabled();
  state_save();
  layer_mark_dirty(s_layer);
}

static void accel_tap_handler(AccelAxisType axis, int32_t direction) {
  s_state.n_accel++;
  s_state.mode = (s_state.mode + 1) % MODE_COUNT;
  apply_mode();
}

static const char *mode_name(uint8_t mode) {
  switch (mode) {
    case MODE_RAW: return "RAW";
    case MODE_RAW_NOBRIDGE: return "RAW-NB";
    case MODE_RECOG: return "RECOG";
    default: return "?";
  }
}

static void draw(Layer *layer, GContext *ctx) {
  const GRect bounds = layer_get_bounds(layer);
  char buf[48];

  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);
  graphics_context_set_text_color(ctx, GColorWhite);

  const GFont f_title = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
  const GFont f_mode = fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD);
  const GFont f_body = fonts_get_system_font(FONT_KEY_GOTHIC_14);

  graphics_draw_text(ctx, "TOUCH PROBE", f_title, GRect(4, 0, bounds.size.w - 8, 22),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);

  // The mode is the biggest thing on screen: it is what an accelerometer tap is
  // meant to change, so it doubles as proof the probe is running at all.
  graphics_draw_text(ctx, mode_name(s_state.mode), f_mode, GRect(4, 18, bounds.size.w - 8, 34),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);

  snprintf(buf, sizeof(buf), "enabled %s   lit %s", s_touch_enabled ? "YES" : "NO",
           light_is_on() ? "Y" : "N");
  graphics_draw_text(ctx, buf, f_body, GRect(4, 54, bounds.size.w - 8, 18),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);

  snprintf(buf, sizeof(buf), "down %lu  move %lu  up %lu", (unsigned long)s_state.n_down,
           (unsigned long)s_state.n_move, (unsigned long)s_state.n_up);
  graphics_draw_text(ctx, buf, f_body, GRect(4, 72, bounds.size.w - 8, 18),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);

  const char *nn = (s_state.nonnav == 0) ? "-" : (s_state.nonnav == 2 ? "YES" : "no");
  snprintf(buf, sizeof(buf), "xy %d,%d   nonnav %s", s_state.last_x, s_state.last_y, nn);
  graphics_draw_text(ctx, buf, f_body, GRect(4, 90, bounds.size.w - 8, 18),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);

  snprintf(buf, sizeof(buf), "recog taps %lu", (unsigned long)s_state.n_recog);
  graphics_draw_text(ctx, buf, f_body, GRect(4, 108, bounds.size.w - 8, 18),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);

  snprintf(buf, sizeof(buf), "accel taps %lu", (unsigned long)s_state.n_accel);
  graphics_draw_text(ctx, buf, f_body, GRect(4, 126, bounds.size.w - 8, 18),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);

  snprintf(buf, sizeof(buf), "launches %lu", (unsigned long)s_state.n_launch);
  graphics_draw_text(ctx, buf, f_body, GRect(4, 144, bounds.size.w - 8, 18),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);

  // A marker at the last reported point, so a touch that lands can be told from
  // one that reports a stale or bogus coordinate.
  if (s_state.nonnav != 0 || s_state.n_recog > 0) {
    graphics_context_set_fill_color(ctx, GColorRed);
    graphics_fill_circle(ctx, GPoint(s_state.last_x, s_state.last_y), 6);
  }

  // Seconds tick so a frozen probe is obvious at a glance.
  const time_t now = time(NULL);
  const struct tm *lt = localtime(&now);
  strftime(buf, sizeof(buf), "%H:%M:%S", lt);
  graphics_context_set_text_color(ctx, GColorWhite);
  graphics_draw_text(ctx, buf, f_title, GRect(4, bounds.size.h - 26, bounds.size.w - 8, 24),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
}

static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  s_touch_enabled = touch_service_is_enabled();
  if (units_changed & MINUTE_UNIT) state_save();
  layer_mark_dirty(s_layer);
}

static void window_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  s_layer = layer_create(layer_get_bounds(root));
  layer_set_update_proc(s_layer, draw);
  layer_add_child(root, s_layer);

  s_tap_recognizer = tap_recognizer_create(recognizer_handler, NULL);
  apply_mode();
}

static void window_unload(Window *window) {
  state_save();
  if (s_recognizer_attached) {
    window_detach_recognizer(window, s_tap_recognizer);
    s_recognizer_attached = false;
  }
  recognizer_destroy(s_tap_recognizer);
  layer_destroy(s_layer);
}

static void init(void) {
  state_load();
  s_window = window_create();
  window_set_window_handlers(s_window, (WindowHandlers){
                                           .load = window_load,
                                           .unload = window_unload,
                                       });
  window_stack_push(s_window, true);

  accel_tap_service_subscribe(accel_tap_handler);
  tick_timer_service_subscribe(SECOND_UNIT, tick_handler);
}

static void deinit(void) {
  state_save();
  tick_timer_service_unsubscribe();
  accel_tap_service_unsubscribe();
  if (s_touch_subscribed) touch_service_unsubscribe();
  window_destroy(s_window);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
