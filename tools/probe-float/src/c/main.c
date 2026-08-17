// Staged floating-point probe.
//
// The bisect established that our toolchain and project setup are fine (both
// TimeStyle's source built here and an integer-only probe run on the watch)
// while the probe that uses libm and soft-float doubles crashes. This narrows
// that to a single step.
//
// The watch gives no logs and a crash inside a layer update proc draws nothing,
// so this records its progress to persistent storage *before* each step. A
// crash cannot unwrite that, so the next launch reports how far the previous
// run got:
//
//     reached: <name of the last stage started>
//     runs: <how many times this has been launched>
//
// The float work runs from a timer after the first render, not during it, so
// the report is on screen before anything risky happens.
//
// Reading the result: if a stage is named, that stage is where it died. "done"
// means every stage passed, which would point back at the watchface's own code
// rather than at floating point.

#include <pebble.h>

#include <math.h>

#include "hebdate.h"
#include "solar.h"

#define PERSIST_STAGE 1
#define PERSIST_RUNS 2

// Coarse coordinates, deliberately not anyone's exact position.
#define PROBE_LAT 39.95
#define PROBE_LON (-75.17)

// Stage 0 is "nothing started". Names are what appears on screen, so they have
// to be short enough to read on a 200px display.
static const char *const STAGE_NAMES[] = {
    "none",        // 0
    "dbl add",     // 1  __aeabi_dadd / dsub
    "dbl mul",     // 2  __aeabi_dmul
    "dbl div",     // 3  __aeabi_ddiv
    "dbl cmp",     // 4  __aeabi_dcmp*
    "dbl->int",    // 5  __aeabi_d2iz
    "int->dbl",    // 6  __aeabi_i2d / l2d
    "floor",       // 7
    "round",       // 8
    "fmod",        // 9
    "sin",         // 10
    "cos",         // 11
    "tan",         // 12
    "asin",        // 13
    "acos",        // 14
    "atan2",       // 15
    "solar",       // 16 the whole NOAA bracket
    "hebdate",     // 17 the calendar, which also uses double and floor
    "done",        // 18
};
#define STAGE_COUNT ((int)(sizeof(STAGE_NAMES) / sizeof(STAGE_NAMES[0])))

static Window *s_window;
static Layer *s_canvas;
static int s_last_stage;
static int s_runs;
static AppTimer *s_timer;

// volatile so the compiler cannot fold these away or reorder them across the
// persist writes that mark how far we got.
static volatile double s_v;
static volatile int s_i;

static void mark(int stage) {
  persist_write_int(PERSIST_STAGE, stage);
}

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

  char buf[40];
  const char *name = (s_last_stage >= 0 && s_last_stage < STAGE_COUNT)
                         ? STAGE_NAMES[s_last_stage]
                         : "??";

  draw_line(ctx, "float probe", 8, b.size.w);
  snprintf(buf, sizeof(buf), "reached:");
  draw_line(ctx, buf, 52, b.size.w);
  snprintf(buf, sizeof(buf), "%s", name);
  draw_line(ctx, buf, 84, b.size.w);
  snprintf(buf, sizeof(buf), "runs %d", s_runs);
  draw_line(ctx, buf, 128, b.size.w);
}

// Each step marks itself *before* doing the work, so a crash leaves that stage
// recorded as the one that failed.
static void run_stages(void *ctx) {
  s_timer = NULL;

  mark(1);  s_v = 1.5 + 2.25;  s_v = s_v - 0.75;
  mark(2);  s_v = s_v * 3.125;
  mark(3);  s_v = s_v / 7.0;
  mark(4);  s_i = (s_v > 0.5) ? 1 : 0;
  mark(5);  s_i = (int)s_v;
  mark(6);  s_v = (double)s_i + (double)(long)1234567;
  mark(7);  s_v = floor(s_v + 0.5);
  mark(8);  s_v = round(s_v * 1.5);
  mark(9);  s_v = fmod(s_v, 360.0);
  mark(10); s_v = sin(s_v);
  mark(11); s_v = cos(s_v);
  mark(12); s_v = tan(s_v * 0.25);
  mark(13); s_v = asin(0.5);
  mark(14); s_v = acos(0.5);
  mark(15); s_v = atan2(1.0, 2.0);

  mark(16);
  SolarBracket br = solar_bracket((double)time(NULL) * 1000.0, PROBE_LAT, PROBE_LON);
  s_i = br.valid ? 1 : 0;

  mark(17);
  time_t now = time(NULL);
  struct tm *lt = localtime(&now);
  if (lt) {
    HebrewDate hd = hebdate_for_now(lt->tm_year + 1900, lt->tm_mon + 1, lt->tm_mday,
                                    lt->tm_hour, true);
    s_i = hd.day;
  }

  mark(18);  // done: everything above survived
  s_last_stage = 18;
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
  // Report the previous run before risking anything.
  s_last_stage = persist_exists(PERSIST_STAGE) ? persist_read_int(PERSIST_STAGE) : 0;
  s_runs = (persist_exists(PERSIST_RUNS) ? persist_read_int(PERSIST_RUNS) : 0) + 1;
  persist_write_int(PERSIST_RUNS, s_runs);

  s_window = window_create();
  window_set_background_color(s_window, GColorBlack);
  window_set_window_handlers(s_window, (WindowHandlers){
                                           .load = window_load,
                                           .unload = window_unload,
                                       });
  window_stack_push(s_window, true);

  // Long enough that the report is definitely on screen and readable first.
  s_timer = app_timer_register(4000, run_stages, NULL);

  app_event_loop();
  window_destroy(s_window);
}
