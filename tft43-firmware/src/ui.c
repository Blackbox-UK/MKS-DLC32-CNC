// Screens: main readout (DRO) with job controls, jog, SD files, more.
#include <stdio.h>
#include <string.h>
#include "board.h"
#include "gfx.h"
#include "grbl.h"
#include "hw.h"
#include "ui.h"

bool ui_request_calibration;

// ---------------------------------------------------------------- buttons
typedef struct {
  int16_t x, y, w, h;
  uint8_t id;
  char label[24];
  uint16_t color;
  bool enabled;
} button_t;

#define MAX_BUTTONS 24
static button_t buttons[MAX_BUTTONS];
static int nbuttons;
static int pressed = -1;  // index of the button under the finger

enum {
  B_NONE,
  // main
  B_ZERO_X, B_ZERO_Y, B_ZERO_Z, B_JOG, B_HOME, B_UNLOCK, B_FILES, B_MORE,
  B_PAUSE, B_RESUME, B_STOP, B_FEED_MINUS, B_FEED_PLUS, B_FEED_100,
  // jog
  B_XM, B_XP, B_YM, B_YP, B_ZM, B_ZP, B_STEP0, B_STEP1, B_STEP2, B_STEP3,
  B_FEED0, B_FEED1, B_FEED2, B_BACK, B_ZERO_XY, B_JOG_STOP,
  // files
  B_UP, B_DOWN, B_REFRESH, B_RUN, B_ROW0, B_ROW1, B_ROW2, B_ROW3, B_ROW4,
  B_CONFIRM_RUN, B_CANCEL,
  // more
  B_CALIBRATE, B_RESET, B_ZERO_ALL, B_GOTO_ZERO, B_SPINDLE_OFF,
};

static void add_button(int x, int y, int w, int h, uint8_t id, const char * label, uint16_t color, bool enabled)
{
  if (nbuttons >= MAX_BUTTONS) return;
  button_t * b = &buttons[nbuttons++];
  b->x = x; b->y = y; b->w = w; b->h = h; b->id = id;
  strncpy(b->label, label, sizeof(b->label) - 1);
  b->label[sizeof(b->label) - 1] = 0;
  b->color = color;
  b->enabled = enabled;
}

static void draw_button(const button_t * b, bool down)
{
  uint16_t fill = !b->enabled ? C_PANEL : down ? C_PRESSED : b->color;
  uint16_t fg = b->enabled ? C_TEXT : C_DIM;
  gfx_round(b->x, b->y, b->w, b->h, 6, fill, C_BG);
  const font_t * f = (gfx_text_width(&font_medium, b->label) <= b->w - 8) ? &font_medium : &font_small;
  int tw = gfx_text_width(f, b->label);
  if (tw > b->w - 4) tw = b->w - 4;
  gfx_text_box(f, b->x + (b->w - tw) / 2, b->y + 6, tw, b->h - 12, ALIGN_CENTER, b->label, fg, fill);
}

// ---------------------------------------------------------------- state
static screen_t screen = SCR_MAIN;
static uint32_t layout_key = 0xFFFFFFFF;  // forces a full redraw when it changes
static screen_t drawn_screen = (screen_t)-1;
static bool drawn_confirm;

static const float steps[4] = {0.1f, 1.0f, 10.0f, 50.0f};
static const int feeds[3] = {300, 1000, 2500};
static int step_sel = 1, feed_sel = 1;

static int file_top, file_sel = -1;
static bool confirm_run;
static uint32_t files_seq_seen;
static bool stop_pending;  // feed hold sent, reset once motion has stopped
static uint32_t stop_at;

// cached text of dynamic fields, so only changes are redrawn
static char cache[16][48];
static uint32_t msg_seq_seen;
static uint32_t msg_shown_at;

static void invalidate(void)
{
  memset(cache, 0xFF, sizeof(cache));
  msg_seq_seen = grbl.msg_seq - 1;
}

static bool changed(int slot, const char * s)
{
  if (!strncmp(cache[slot], s, sizeof(cache[slot]))) return false;
  strncpy(cache[slot], s, sizeof(cache[slot]) - 1);
  cache[slot][sizeof(cache[slot]) - 1] = 0;
  return true;
}

static void fmt_fixed(char * out, int n, float v, int decimals)
{
  bool neg = v < 0;
  if (neg) v = -v;
  int scale = decimals == 3 ? 1000 : decimals == 2 ? 100 : decimals == 1 ? 10 : 1;
  long iv = (long)(v * scale + 0.5f);
  if (iv == 0) neg = false;
  if (decimals) snprintf(out, n, "%s%ld.%0*ld", neg ? "-" : "", iv / scale, decimals, iv % scale);
  else snprintf(out, n, "%s%ld", neg ? "-" : "", iv);
}

static bool job_active(void)
{
  return grbl.sd_running || grbl.state == ST_RUN || grbl.state == ST_HOLD;
}

static bool can_move(void)
{
  return grbl.connected && (grbl.state == ST_IDLE || grbl.state == ST_JOG);
}

// ---------------------------------------------------------------- top bar
#define BAR_H 32

static void state_colors(uint16_t * bg, uint16_t * fg)
{
  *fg = C_BLACK;
  switch (grbl.state) {
    case ST_IDLE: *bg = C_GREEN; break;
    case ST_RUN: case ST_JOG: case ST_HOME: *bg = C_ACCENT; break;
    case ST_HOLD: case ST_DOOR: *bg = C_YELLOW; break;
    case ST_ALARM: *bg = C_RED; *fg = C_WHITE; break;
    case ST_CHECK: case ST_SLEEP: *bg = C_ORANGE; break;
    default: *bg = C_BUTTON; *fg = C_DIM; break;
  }
}

static void draw_bar(void)
{
  char st[24];
  snprintf(st, sizeof(st), "%s", grbl.connected ? grbl.state_text : "No link");
  if (changed(0, st)) {
    uint16_t bg, fg;
    state_colors(&bg, &fg);
    gfx_round(4, 3, 112, BAR_H - 6, 5, bg, C_PANEL);
    gfx_text_box(&font_medium, 10, 5, 100, BAR_H - 10, ALIGN_CENTER, st, fg, bg);
  }
  if (grbl.msg_seq != msg_seq_seen) {
    msg_seq_seen = grbl.msg_seq;
    msg_shown_at = millis();
    gfx_text_box(&font_small, 124, 2, LCD_WIDTH - 128, BAR_H - 4, ALIGN_LEFT, grbl.msg,
                 grbl.msg_is_error ? C_RED : C_DIM, C_PANEL);
  } else if (grbl.msg[0] && millis() - msg_shown_at > (grbl.msg_is_error ? 20000u : 6000u)) {
    grbl.msg[0] = 0;  // info messages fade after a few seconds, errors after longer
    gfx_fill(124, 2, LCD_WIDTH - 128, BAR_H - 4, C_PANEL);
  }
}

// ---------------------------------------------------------------- main screen
#define ROW_Y0 38
#define ROW_H  52

static void main_layout(void)
{
  bool job = job_active();
  bool mv = can_move();
  for (int a = 0; a < 3; a++)
    add_button(396, ROW_Y0 + a * ROW_H + 4, 80, ROW_H - 8, B_ZERO_X + a, a == 0 ? "X=0" : a == 1 ? "Y=0" : "Z=0",
               C_BUTTON, mv);
  int y = 228, h = 40, w = 90, gap = 6, x = 4;
  if (job) {
    bool hold = grbl.state == ST_HOLD;
    add_button(x, y, w, h, hold ? B_RESUME : B_PAUSE, hold ? "Resume" : "Pause", hold ? C_GREEN : C_ORANGE, true);
    x += w + gap;
    add_button(x, y, w, h, B_STOP, "Stop", C_RED, true); x += w + gap;
    add_button(x, y, w, h, B_FEED_MINUS, "Feed-", C_BUTTON, true); x += w + gap;
    add_button(x, y, w, h, B_FEED_100, "Feed100", C_BUTTON, true); x += w + gap;
    add_button(x, y, w, h, B_FEED_PLUS, "Feed+", C_BUTTON, true);
  } else {
    bool alarm = grbl.state == ST_ALARM;
    add_button(x, y, w, h, B_JOG, "Jog", C_BUTTON, mv); x += w + gap;
    add_button(x, y, w, h, B_HOME, "Home", C_BUTTON, grbl.connected && (mv || alarm)); x += w + gap;
    add_button(x, y, w, h, B_UNLOCK, "Unlock", alarm ? C_ORANGE : C_BUTTON, grbl.connected && alarm); x += w + gap;
    add_button(x, y, w, h, B_FILES, "Files", C_BUTTON, mv); x += w + gap;
    add_button(x, y, w, h, B_MORE, "More", C_BUTTON, true);
  }
}

static void main_static(void)
{
  for (int a = 0; a < 3; a++) {
    int y = ROW_Y0 + a * ROW_H;
    gfx_round(4, y + 2, 388, ROW_H - 4, 6, C_PANEL, C_BG);
    char l[2] = {"XYZ"[a], 0};
    gfx_text(&font_big, 12, y + 3, l, C_ACCENT, C_PANEL);
    gfx_text(&font_small, 310, y + 8, "Machine", C_DIM, C_PANEL);
  }
}

static void main_dynamic(void)
{
  char s[48];
  for (int a = 0; a < 3; a++) {
    int y = ROW_Y0 + a * ROW_H;
    // cache key carries the link state, so the colour updates even if the value doesn't
    s[0] = grbl.connected ? 'c' : 'd';
    fmt_fixed(s + 1, sizeof(s) - 1, grbl.wpos[a], 3);
    if (changed(1 + a, s))
      gfx_text_box(&font_big, 44, y + 3, 256, 46, ALIGN_RIGHT, s + 1, grbl.connected ? C_TEXT : C_DIM, C_PANEL);
    fmt_fixed(s, sizeof(s), grbl.mpos[a], 2);
    if (changed(4 + a, s))
      gfx_text_box(&font_small, 304, y + 26, 84, 18, ALIGN_RIGHT, s, C_DIM, C_PANEL);
  }

  int y = 196;
  if (job_active()) {
    // progress bar with file name and percent
    const char * name = grbl.sd_file[0] == '/' ? grbl.sd_file + 1 : grbl.sd_file;
    snprintf(s, sizeof(s), "%.40s", grbl.sd_running ? name : "Running");
    if (changed(7, s)) gfx_text_box(&font_small, 8, y, 300, 26, ALIGN_LEFT, s, C_TEXT, C_BG);
    char p[24];
    fmt_fixed(p, sizeof(p), grbl.sd_pct, 1);
    strcat(p, "%");
    if (changed(8, p)) {
      gfx_text_box(&font_small, 410, y, 66, 26, ALIGN_RIGHT, p, C_TEXT, C_BG);
      int bw = 96, fill = (int)(grbl.sd_pct * bw / 100);
      if (fill > bw) fill = bw;
      gfx_fill(310, y + 8, fill, 10, C_ACCENT);
      gfx_fill(310 + fill, y + 8, bw - fill, 10, C_BUTTON);
    }
    snprintf(s, sizeof(s), "F%d %d%%", (int)grbl.feed, grbl.ov_feed);
  } else {
    snprintf(s, sizeof(s), "F%d", (int)grbl.feed);
    if (changed(7, "")) gfx_fill(8, y, 470, 26, C_BG);
    changed(8, "");
  }
  char s2[48];
  snprintf(s2, sizeof(s2), "S%d %d%%", (int)grbl.spindle, grbl.ov_spindle);
  if (!job_active()) {
    if (changed(9, s)) gfx_text_box(&font_small, 8, y, 150, 26, ALIGN_LEFT, s, C_DIM, C_BG);
    if (changed(10, s2)) gfx_text_box(&font_small, 160, y, 150, 26, ALIGN_LEFT, s2, C_DIM, C_BG);
    if (changed(11, grbl.pins)) {
      char pn[24] = "";
      if (grbl.pins[0]) snprintf(pn, sizeof(pn), "Pin %.12s", grbl.pins);
      gfx_text_box(&font_small, 320, y, 156, 26, ALIGN_RIGHT, pn, C_YELLOW, C_BG);
    }
  } else {
    changed(9, ""); changed(10, ""); changed(11, "\x01");
  }
}

// ---------------------------------------------------------------- jog screen
static void jog_layout(void)
{
  bool mv = can_move();
  const int bw = 72, bh = 58, x0 = 6, y0 = 40;
  add_button(x0 + bw + 4, y0, bw, bh, B_YP, "Y+", C_BUTTON, mv);
  add_button(x0, y0 + bh + 4, bw, bh, B_XM, "X-", C_BUTTON, mv);
  add_button(x0 + 2 * (bw + 4), y0 + bh + 4, bw, bh, B_XP, "X+", C_BUTTON, mv);
  add_button(x0 + bw + 4, y0 + 2 * (bh + 4), bw, bh, B_YM, "Y-", C_BUTTON, mv);
  add_button(x0 + bw + 4, y0 + bh + 4, bw, bh, B_JOG_STOP, "Stop", C_RED, grbl.state == ST_JOG);
  add_button(x0 + 3 * (bw + 4) + 8, y0, bw, bh, B_ZP, "Z+", C_BUTTON, mv);
  add_button(x0 + 3 * (bw + 4) + 8, y0 + 2 * (bh + 4), bw, bh, B_ZM, "Z-", C_BUTTON, mv);

  static const char * step_l[4] = {"0.1", "1", "10", "50"};
  for (int i = 0; i < 4; i++)
    add_button(330 + i * 37, 112, 34, 40, B_STEP0 + i, step_l[i], i == step_sel ? C_ACCENT : C_BUTTON, true);
  static const char * feed_l[3] = {"300", "1000", "2500"};
  for (int i = 0; i < 3; i++)
    add_button(330 + i * 49, 178, 46, 40, B_FEED0 + i, feed_l[i], i == feed_sel ? C_ACCENT : C_BUTTON, true);

  add_button(4, 228, 110, 40, B_BACK, "Back", C_BUTTON, true);
  add_button(120, 228, 110, 40, B_ZERO_XY, "XY=0", C_BUTTON, mv);
  add_button(236, 228, 110, 40, B_ZERO_Z, "Z=0", C_BUTTON, mv);
  add_button(352, 228, 124, 40, B_HOME, "Home", C_BUTTON, mv || grbl.state == ST_ALARM);
}

static void jog_static(void)
{
  gfx_text(&font_small, 330, 92, "Step mm", C_DIM, C_BG);
  gfx_text(&font_small, 330, 158, "Feed mm/min", C_DIM, C_BG);
}

static void jog_dynamic(void)
{
  char s[24], v[16];
  for (int a = 0; a < 3; a++) {
    fmt_fixed(v, sizeof(v), grbl.wpos[a], 3);
    snprintf(s, sizeof(s), "%c%9s", "XYZ"[a], v);
    if (changed(1 + a, s)) gfx_text_box(&font_small, 330, 38 + a * 18, 146, 18, ALIGN_LEFT, s, C_TEXT, C_BG);
  }
}

static void jog(int axis, int dir)
{
  char cmd[64], d[16];
  fmt_fixed(d, sizeof(d), steps[step_sel] * dir, 3);
  snprintf(cmd, sizeof(cmd), "$J=G91 G21 %c%s F%d", "XYZ"[axis], d, feeds[feed_sel]);
  grbl_send(cmd);
}

// ---------------------------------------------------------------- files screen
#define FILE_ROWS 5
#define FROW_H 36

static void files_layout(void)
{
  if (confirm_run) {
    add_button(100, 170, 130, 46, B_CANCEL, "Cancel", C_BUTTON, true);
    add_button(250, 170, 130, 46, B_CONFIRM_RUN, "Run", C_GREEN, can_move());
    return;
  }
  for (int r = 0; r < FILE_ROWS; r++) {
    int i = file_top + r;
    if (i >= grbl.file_count) break;
    add_button(4, 38 + r * FROW_H, 392, FROW_H - 4, B_ROW0 + r, "", C_BG, true);  // invisible hit area
  }
  add_button(402, 38, 74, 84, B_UP, "Up", C_BUTTON, file_top > 0);
  add_button(402, 128, 74, 84, B_DOWN, "Down", C_BUTTON, file_top + FILE_ROWS < grbl.file_count);
  add_button(4, 228, 110, 40, B_BACK, "Back", C_BUTTON, true);
  add_button(120, 228, 130, 40, B_REFRESH, "Refresh", C_BUTTON, grbl.connected && !grbl.files_loading);
  add_button(352, 228, 124, 40, B_RUN, "Run", C_GREEN, file_sel >= 0 && can_move());
}

static void draw_file_row(int r)
{
  int i = file_top + r;
  int y = 38 + r * FROW_H;
  if (i >= grbl.file_count) { gfx_fill(4, y, 392, FROW_H - 4, C_BG); return; }
  uint16_t bg = (i == file_sel) ? C_ACCENT : C_PANEL;
  uint16_t fg = (i == file_sel) ? C_BLACK : C_TEXT;
  gfx_round(4, y, 392, FROW_H - 4, 5, bg, C_BG);
  const char * n = grbl.file_name[i][0] == '/' ? grbl.file_name[i] + 1 : grbl.file_name[i];
  gfx_text_box(&font_small, 12, y + 2, 290, FROW_H - 8, ALIGN_LEFT, n, fg, bg);
  char sz[16];
  uint32_t b = grbl.file_size[i];
  if (b >= 1048576) snprintf(sz, sizeof(sz), "%lu.%lu MB", (unsigned long)(b / 1048576), (unsigned long)(b % 1048576 * 10 / 1048576));
  else snprintf(sz, sizeof(sz), "%lu KB", (unsigned long)((b + 1023) / 1024));
  gfx_text_box(&font_small, 304, y + 2, 86, FROW_H - 8, ALIGN_RIGHT, sz, fg, bg);
}

static void files_static(void)
{
  if (confirm_run) {
    gfx_round(40, 50, 400, 180, 10, C_PANEL, C_BG);
    gfx_text_box(&font_medium, 50, 66, 380, 30, ALIGN_CENTER, "Run this file?", C_TEXT, C_PANEL);
    const char * n = grbl.file_name[file_sel][0] == '/' ? grbl.file_name[file_sel] + 1 : grbl.file_name[file_sel];
    gfx_text_box(&font_small, 50, 104, 380, 24, ALIGN_CENTER, n, C_ACCENT, C_PANEL);
    gfx_text_box(&font_small, 50, 132, 380, 24, ALIGN_CENTER, "Check zero and tool first", C_DIM, C_PANEL);
    return;
  }
  for (int r = 0; r < FILE_ROWS; r++) draw_file_row(r);
}

static void files_dynamic(void)
{
  if (confirm_run) return;
  const char * s = grbl.files_loading ? "Reading SD card..." : grbl.file_count == 0 ? "No files on the controller's SD card" : "";
  if (changed(1, s)) gfx_text_box(&font_small, 8, 200, 390, 24, ALIGN_LEFT, s, C_DIM, C_BG);
}

// ---------------------------------------------------------------- more screen
static void more_layout(void)
{
  bool mv = can_move();
  add_button(4, 40, 230, 46, B_CALIBRATE, "Calibrate touch", C_BUTTON, true);
  add_button(246, 40, 230, 46, B_RESET, "Reset controller", C_RED, grbl.connected);
  add_button(4, 94, 230, 46, B_ZERO_ALL, "Zero X Y Z", C_BUTTON, mv);
  add_button(246, 94, 230, 46, B_GOTO_ZERO, "Go to XY zero", C_BUTTON, mv);
  add_button(4, 148, 230, 46, B_SPINDLE_OFF, "Spindle off", C_BUTTON, mv);
  add_button(4, 228, 110, 40, B_BACK, "Back", C_BUTTON, true);
}

static void more_static(void)
{
  gfx_text(&font_small, 246, 152, "Go to XY zero lifts", C_DIM, C_BG);
  gfx_text(&font_small, 246, 172, "Z to 5 mm first", C_DIM, C_BG);
}

static void more_dynamic(void)
{
  const char * v = grbl.version[0] ? grbl.version : (grbl.connected ? "Grbl" : "Controller not connected");
  if (changed(1, v)) gfx_text_box(&font_small, 120, 202, 356, 20, ALIGN_RIGHT, v, C_DIM, C_BG);
  if (changed(2, "TFT43 Grbl " FW_VERSION))
    gfx_text_box(&font_small, 120, 248, 356, 20, ALIGN_RIGHT, "TFT43 Grbl " FW_VERSION, C_DIM, C_BG);
}

// ---------------------------------------------------------------- actions
static void zero_axes(const char * axes)
{
  char cmd[40];
  snprintf(cmd, sizeof(cmd), "G10 L20 P0 %s", axes);
  grbl_send(cmd);
}

static void on_button(uint8_t id)
{
  switch (id) {
    case B_ZERO_X: zero_axes("X0"); break;
    case B_ZERO_Y: zero_axes("Y0"); break;
    case B_ZERO_Z: zero_axes("Z0"); break;
    case B_ZERO_XY: zero_axes("X0 Y0"); break;
    case B_ZERO_ALL: zero_axes("X0 Y0 Z0"); break;
    case B_JOG: ui_show(SCR_JOG); break;
    case B_HOME: grbl_send("$H"); break;
    case B_UNLOCK: grbl_send("$X"); break;
    case B_FILES:
      ui_show(SCR_FILES);
      if (grbl.file_count == 0) grbl_send("$SD/List");
      break;
    case B_MORE: ui_show(SCR_MORE); break;
    case B_PAUSE: grbl_realtime(RT_HOLD); break;
    case B_RESUME: grbl_realtime(RT_RESUME); break;
    case B_STOP:
      // Feed hold first so the machine decelerates, then reset to abort the job.
      grbl_realtime(RT_HOLD);
      stop_pending = true;
      stop_at = millis();
      break;
    case B_FEED_MINUS: grbl_realtime(RT_FEED_MINUS); break;
    case B_FEED_PLUS: grbl_realtime(RT_FEED_PLUS); break;
    case B_FEED_100: grbl_realtime(RT_FEED_100); break;
    case B_XM: jog(0, -1); break;
    case B_XP: jog(0, 1); break;
    case B_YM: jog(1, -1); break;
    case B_YP: jog(1, 1); break;
    case B_ZM: jog(2, -1); break;
    case B_ZP: jog(2, 1); break;
    case B_JOG_STOP: grbl_realtime(RT_JOG_CANCEL); break;
    case B_STEP0: case B_STEP1: case B_STEP2: case B_STEP3: step_sel = id - B_STEP0; break;
    case B_FEED0: case B_FEED1: case B_FEED2: feed_sel = id - B_FEED0; break;
    case B_BACK: ui_show(SCR_MAIN); break;
    case B_UP: file_top -= FILE_ROWS; if (file_top < 0) file_top = 0; break;
    case B_DOWN: file_top += FILE_ROWS; break;
    case B_REFRESH: file_sel = -1; file_top = 0; grbl_send("$SD/List"); break;
    case B_ROW0: case B_ROW1: case B_ROW2: case B_ROW3: case B_ROW4: {
      int i = file_top + (id - B_ROW0);
      file_sel = (file_sel == i) ? -1 : i;
      layout_key = 0xFFFFFFFF;
      break;
    }
    case B_RUN: if (file_sel >= 0) confirm_run = true; break;
    case B_CANCEL: confirm_run = false; break;
    case B_CONFIRM_RUN: {
      char cmd[80];
      const char * n = grbl.file_name[file_sel];
      snprintf(cmd, sizeof(cmd), "$SD/Run=%s%s", n[0] == '/' ? "" : "/", n);
      grbl_send(cmd);
      confirm_run = false;
      ui_show(SCR_MAIN);
      break;
    }
    case B_CALIBRATE: ui_request_calibration = true; break;
    case B_RESET: grbl_realtime(RT_RESET); break;
    case B_GOTO_ZERO: grbl_send("G90 G0 Z5"); grbl_send("G90 G0 X0 Y0"); break;
    case B_SPINDLE_OFF: grbl_send("M5"); break;
  }
}

// ---------------------------------------------------------------- framework
static uint32_t make_key(void)
{
  // Everything that changes which buttons exist or how they look.
  uint32_t k = screen;
  k = k * 31 + grbl.connected;
  k = k * 31 + grbl.state;
  k = k * 31 + job_active();
  k = k * 31 + step_sel;
  k = k * 31 + feed_sel;
  k = k * 31 + (uint32_t)file_top;
  k = k * 31 + (uint32_t)(file_sel + 1);
  k = k * 31 + confirm_run;
  k = k * 31 + grbl.files_loading;
  k = k * 31 + grbl.files_seq;
  return k;
}

static void build_layout(void)
{
  nbuttons = 0;
  switch (screen) {
    case SCR_MAIN: main_layout(); break;
    case SCR_JOG: jog_layout(); break;
    case SCR_FILES: files_layout(); break;
    case SCR_MORE: more_layout(); break;
  }
}


static void redraw(void)
{
  bool full = (drawn_screen != screen) || (drawn_confirm != confirm_run);
  if (full) {
    gfx_fill(0, 0, LCD_WIDTH, BAR_H, C_PANEL);
    gfx_fill(0, BAR_H, LCD_WIDTH, LCD_HEIGHT - BAR_H, C_BG);
    invalidate();
    drawn_screen = screen;
    drawn_confirm = confirm_run;
    switch (screen) {
      case SCR_MAIN: main_static(); break;
      case SCR_JOG: jog_static(); break;
      case SCR_FILES: files_static(); break;
      case SCR_MORE: more_static(); break;
    }
  } else if (screen == SCR_FILES) {
    files_static();  // selection or page changed
  } else if (screen == SCR_MAIN) {
    // buttons row may change between idle and job layouts
    gfx_fill(0, 226, LCD_WIDTH, LCD_HEIGHT - 226, C_BG);
  }
  build_layout();
  for (int i = 0; i < nbuttons; i++)
    if (buttons[i].color != C_BG) draw_button(&buttons[i], false);
  pressed = -1;
}

void ui_show(screen_t s)
{
  screen = s;
  confirm_run = false;
  layout_key = 0xFFFFFFFF;
  drawn_screen = (screen_t)-1;  // full redraw
}

void ui_init(void)
{
  grbl_init();
  ui_show(SCR_MAIN);
}

static int hit(int x, int y)
{
  for (int i = 0; i < nbuttons; i++) {
    button_t * b = &buttons[i];
    if (x >= b->x && x < b->x + b->w && y >= b->y && y < b->y + b->h) return i;
  }
  return -1;
}

void ui_step(bool down, int x, int y)
{
  // finish a pending Stop: reset once the hold has brought the machine to rest
  if (stop_pending) {
    bool rested = !strncmp(grbl.state_text, "Hold:0", 6) || grbl.state == ST_IDLE;
    if (rested || millis() - stop_at > 5000) {
      grbl_realtime(RT_RESET);
      stop_pending = false;
    }
  }

  if (screen == SCR_FILES && grbl.files_seq != files_seq_seen) {
    files_seq_seen = grbl.files_seq;
    file_sel = -1;
    file_top = 0;
  }
  if (file_top >= grbl.file_count) file_top = 0;

  uint32_t k = make_key();
  if (k != layout_key && pressed < 0) {
    layout_key = k;
    redraw();
  }

  // touch: highlight while held, act on release inside the same button
  static bool was_down;
  static int last_x, last_y;
  if (down) {
    last_x = x; last_y = y;
    if (!was_down) {
      int h = hit(x, y);
      if (h >= 0 && buttons[h].enabled) {
        pressed = h;
        if (buttons[h].color != C_BG) draw_button(&buttons[h], true);
        beep(30);
      }
    }
  } else if (was_down && pressed >= 0) {
    button_t b = buttons[pressed];
    bool inside = hit(last_x, last_y) == pressed;
    if (b.color != C_BG) draw_button(&buttons[pressed], false);
    pressed = -1;
    if (inside) on_button(b.id);
    layout_key = 0xFFFFFFFF;  // actions usually change the layout
  }
  was_down = down;

  draw_bar();
  switch (screen) {
    case SCR_MAIN: main_dynamic(); break;
    case SCR_JOG: jog_dynamic(); break;
    case SCR_FILES: files_dynamic(); break;
    case SCR_MORE: more_dynamic(); break;
  }
}
