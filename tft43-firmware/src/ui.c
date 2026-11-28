// Screens, navigation bar, overlays (confirm, keyboard) and touch handling.
//
// Screen map (the stock MKS TS24/TS35 menus plus a readout and a G-code console):
//   DRO     live X/Y/Z, zero buttons, alarm help, job progress and job controls
//   Move    jog X/Y/Z, step and speed, home, zero, go to zero, probe Z   (MKS "Move")
//   Files   SD card list on the DLC32, run with confirmation            (MKS "Carve"/"inFile")
//   Tool    spindle on/off and speed, coolant, spindle override          (MKS "Power"/"Tool")
//   Code    G-code console: log, current job line, keyboard to send      (new)
//   Setup   touch calibration, controller reset, WiFi, buzzer, about    (MKS "Settings"/"WiFi")
#include <stdio.h>
#include <string.h>
#include "board.h"
#include "gfx.h"
#include "icons.h"
#include "grbl.h"
#include "hw.h"
#include "ui.h"

bool ui_request_calibration;

#define BAR_H   30   // top status bar
#define NAV_Y   228  // bottom navigation bar
#define NAV_H   (LCD_HEIGHT - NAV_Y)
#define TOP     (BAR_H + 4)
#define C_CONSOLE RGB(12, 15, 20)

// ---------------------------------------------------------------- buttons
enum { ST_NORMAL, ST_SELECTED, ST_TAB, ST_TAB_ACTIVE, ST_HIDDEN };

typedef struct {
  int16_t x, y, w, h;
  uint8_t id;
  int8_t icon;
  uint8_t style;
  bool enabled;
  uint16_t color;
  char label[24];
} button_t;

#define MAX_BUTTONS 64
static button_t buttons[MAX_BUTTONS];
static int nbuttons;
static int pressed = -1;

enum {
  B_NONE,
  B_NAV0, B_NAV1, B_NAV2, B_NAV3, B_NAV4, B_NAV5,
  // DRO
  B_ZERO_X, B_ZERO_Y, B_ZERO_Z, B_UNLOCK, B_HOME, B_PAUSE, B_RESUME, B_STOP, B_FEED_MINUS, B_FEED_PLUS,
  // Move
  B_XM, B_XP, B_YM, B_YP, B_ZM, B_ZP, B_JOG_STOP, B_STEP0, B_STEP1, B_STEP2, B_STEP3, B_FEED0, B_FEED1,
  B_FEED2, B_ZERO_XY, B_GOTO_ZERO, B_PROBE,
  // Files
  B_UP, B_DOWN, B_REFRESH, B_RUN, B_ROW0, B_ROW1, B_ROW2, B_ROW3, B_ROW4,
  // Tool
  B_SPIN_OFF, B_SPIN0, B_SPIN1, B_SPIN2, B_SPIN3, B_FLOOD_ON, B_FLOOD_OFF, B_SOVR_MINUS, B_SOVR_100,
  B_SOVR_PLUS, B_FOVR_100,
  // Code
  B_KEYBOARD, B_CLEAR_LOG,
  // Setup
  B_CALIBRATE, B_RESET, B_WIFI, B_BUZZER, B_SETTINGS_REPORT,
  // WiFi
  B_SSID, B_PASS, B_CONNECT, B_GET_IP, B_BACK,
  // overlays
  B_OK, B_CANCEL, B_KEY,  // B_KEY + index for keyboard keys
};

static void add(int x, int y, int w, int h, uint8_t id, int icon, const char * label, uint16_t color, bool enabled)
{
  if (nbuttons >= MAX_BUTTONS) return;
  button_t * b = &buttons[nbuttons++];
  b->x = x; b->y = y; b->w = w; b->h = h; b->id = id; b->icon = icon;
  b->style = ST_NORMAL;
  b->enabled = enabled;
  b->color = color;
  strncpy(b->label, label ? label : "", sizeof(b->label) - 1);
  b->label[sizeof(b->label) - 1] = 0;
}

static button_t * last(void) { return &buttons[nbuttons - 1]; }

static void draw_button(const button_t * b, bool down)
{
  if (b->style == ST_HIDDEN) return;
  uint16_t base, top, bottom, fg;
  int face_y = b->y, face_h = b->h;

  if (b->style == ST_TAB || b->style == ST_TAB_ACTIVE) {
    bool act = b->style == ST_TAB_ACTIVE;
    base = act ? gfx_lighten(C_TAB, 40) : C_TAB;
    top = down ? gfx_darken(base, 40) : gfx_lighten(base, act ? 50 : 25);
    bottom = gfx_darken(base, 50);
    gfx_vgradient(b->x, b->y, b->w, b->h, top, bottom);
    gfx_fill(b->x, b->y, 1, b->h, gfx_lighten(C_TAB, 30));           // separators
    gfx_fill(b->x + b->w - 1, b->y, 1, b->h, gfx_darken(C_TAB, 80));
    if (act) gfx_fill(b->x, b->y, b->w, 3, C_ACCENT);
    fg = act ? C_TEXT : C_DIM;
  } else {
    base = !b->enabled ? C_PANEL : b->style == ST_SELECTED ? C_ACCENT : b->color;
    gfx_bevel(b->x, b->y, b->w, b->h, 7, base, C_BG, down);
    gfx_bevel_face(base, down, &top, &bottom);
    fg = b->enabled ? C_TEXT : gfx_mix(C_DIM, C_PANEL, 90);
  }

  const font_t * f = &font_medium;
  int tw = gfx_text_width(f, b->label);
  int iw = b->icon >= 0 ? ICON_SIZE : 0;
  int gap = (iw && tw) ? 6 : 0;
  if (iw + gap + tw > b->w - 8) { f = &font_small; tw = gfx_text_width(f, b->label); }
  if (iw + gap + tw > b->w - 4 && iw) { tw = 0; gap = 0; }  // too narrow: icon only
  int x = b->x + (b->w - iw - gap - tw) / 2 + (down ? 1 : 0);
  int ytxt = b->y + (b->h - f->h) / 2 + (down ? 1 : 0);
  uint16_t icol = (b->style == ST_TAB_ACTIVE) ? C_ACCENT : fg;
  if (iw) gfx_icon(b->icon, x, b->y + (b->h - ICON_SIZE) / 2 + (down ? 1 : 0), icol, top, bottom, face_y, face_h);
  if (tw) {
    char buf[24];
    int n = (b->w - 4 - iw - gap) / f->w;
    snprintf(buf, sizeof(buf), "%.*s", n, b->label);
    gfx_text_on(f, x + iw + gap, ytxt, buf, fg, top, bottom, face_y, face_h);
  }
}

// ---------------------------------------------------------------- state
typedef enum { OV_NONE, OV_CONFIRM, OV_KEYBOARD } overlay_t;
typedef enum { KB_GCODE, KB_SSID, KB_PASS } kb_target_t;

static screen_t screen = SCR_DRO;
static overlay_t overlay;
static uint32_t layout_key = 0xFFFFFFFF;
static int drawn_screen = -1, drawn_overlay = -1;

static const float steps[4] = {0.1f, 1.0f, 10.0f, 50.0f};
static const int feeds[3] = {300, 1000, 2500};
static int step_sel = 1, feed_sel = 1;
static int spin_pct = 50;  // selected spindle speed, % of $30

static int file_top, file_sel = -1;
static uint32_t files_seq_seen;
static bool stop_pending;
static uint32_t stop_at;
static bool buzzer_on = true;

// confirm dialog
static char confirm_title[32], confirm_text[48];
static uint8_t confirm_action;
static uint16_t confirm_color;

// keyboard
static kb_target_t kb_target;
static char kb_buf[64];
static bool kb_shift, kb_sym;
static char wifi_ssid[33], wifi_pass[65];

// dynamic field cache (only redraw what changed)
static char cache[24][48];
static uint32_t msg_seq_seen, msg_shown_at, log_seq_seen;

static void invalidate(void)
{
  memset(cache, 0xFF, sizeof(cache));
  msg_seq_seen = grbl.msg_seq - 1;
  log_seq_seen = grbl_log_seq - 1;
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

static bool job_active(void) { return grbl.sd_running || grbl.state == ST_RUN || grbl.state == ST_HOLD; }
static bool can_move(void) { return grbl.connected && (grbl.state == ST_IDLE || grbl.state == ST_JOG); }
static const char * strip_slash(const char * n) { return n[0] == '/' ? n + 1 : n; }

static void ask(const char * title, const char * text, uint8_t action, uint16_t color)
{
  strncpy(confirm_title, title, sizeof(confirm_title) - 1);
  strncpy(confirm_text, text, sizeof(confirm_text) - 1);
  confirm_action = action;
  confirm_color = color;
  overlay = OV_CONFIRM;
}

static void open_keyboard(kb_target_t t, const char * initial)
{
  kb_target = t;
  strncpy(kb_buf, initial, sizeof(kb_buf) - 1);
  kb_buf[sizeof(kb_buf) - 1] = 0;
  kb_shift = (t == KB_GCODE);  // G-code in capitals, WiFi text in lower case
  kb_sym = false;
  overlay = OV_KEYBOARD;
}


// Text on a recessed panel (gfx_inset) that spans py..py+ph, so the text background
// follows the panel's gradient.
static void ptext(int py, int ph, uint16_t base, const font_t * f, int x, int y, int w, int h, int align,
                  const char * str, uint16_t fg)
{
  uint16_t t, b;
  gfx_inset_face(base, &t, &b);
  gfx_text_box_g(f, x, y, w, h, align, str, fg, t, b, py, ph);
}

static void plabel(int py, int ph, uint16_t base, const font_t * f, int x, int y, const char * str, uint16_t fg)
{
  uint16_t t, b;
  gfx_inset_face(base, &t, &b);
  gfx_text_on(f, x, y, str, fg, t, b, py, ph);
}

// ---------------------------------------------------------------- top bar
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

static void bar_static(void)
{
  gfx_vgradient(0, 0, LCD_WIDTH, BAR_H, gfx_lighten(C_BAR, 30), gfx_darken(C_BAR, 40));
  gfx_fill(0, BAR_H, LCD_WIDTH, 1, gfx_darken(C_BAR, 120));
}

static void draw_bar(void)
{
  uint16_t top = gfx_lighten(C_BAR, 30), bot = gfx_darken(C_BAR, 40);
  char st[24];
  snprintf(st, sizeof(st), "%s", grbl.connected ? grbl.state_text : "No link");
  if (changed(0, st)) {
    uint16_t bg, fg, t, b;
    state_colors(&bg, &fg);
    gfx_vgradient(3, 2, 108, BAR_H - 3, top, bot);
    gfx_bevel(4, 3, 104, BAR_H - 8, 6, bg, C_BAR, false);
    gfx_bevel_face(bg, false, &t, &b);
    int tw = gfx_text_width(&font_small, st);
    gfx_text_on(&font_small, 4 + (104 - tw) / 2, 3 + (BAR_H - 8 - font_small.h) / 2, st, fg, t, b, 3, BAR_H - 8);
  }
  if (grbl.msg_seq != msg_seq_seen) {
    msg_seq_seen = grbl.msg_seq;
    msg_shown_at = millis();
    gfx_vgradient(116, 1, LCD_WIDTH - 140, BAR_H - 1, top, bot);
    char m[40];
    snprintf(m, sizeof(m), "%.37s", grbl.msg);
    gfx_text_on(&font_small, 118, (BAR_H - font_small.h) / 2, m, grbl.msg_is_error ? gfx_lighten(C_RED, 60) : C_DIM,
                top, bot, 0, BAR_H);
  } else if (grbl.msg[0] && millis() - msg_shown_at > (grbl.msg_is_error ? 20000u : 6000u)) {
    grbl.msg[0] = 0;
    gfx_vgradient(116, 1, LCD_WIDTH - 140, BAR_H - 1, top, bot);
  }
  // link LED
  if (changed(1, grbl.connected ? "1" : "0")) {
    uint16_t led = grbl.connected ? C_GREEN : C_RED;
    gfx_vgradient(LCD_WIDTH - 22, 4, 18, 22, top, bot);
    gfx_bevel(LCD_WIDTH - 19, 9, 12, 12, 6, led, C_BAR, false);
  }
}

// ---------------------------------------------------------------- navigation
static const struct { int icon; const char * label; } tabs[6] = {
  {ICON_DRO, "DRO"}, {ICON_MOVE, "Move"}, {ICON_FILES, "Files"},
  {ICON_SPINDLE, "Tool"}, {ICON_CONSOLE, "Code"}, {ICON_SETTINGS, "Setup"},
};

static void nav_layout(void)
{
  int active = screen == SCR_WIFI ? SCR_SETUP : (int)screen;
  for (int i = 0; i < 6; i++) {
    add(i * 80, NAV_Y, 80, NAV_H, B_NAV0 + i, tabs[i].icon, tabs[i].label, C_TAB, true);
    last()->style = (i == active) ? ST_TAB_ACTIVE : ST_TAB;
  }
}

// ---------------------------------------------------------------- DRO
#define ROW_Y0 TOP
#define ROW_H  50

static void dro_layout(void)
{
  bool mv = can_move();
  if (job_active()) {
    bool hold = grbl.state == ST_HOLD;
    add(396, ROW_Y0, 80, ROW_H - 4, hold ? B_RESUME : B_PAUSE, hold ? ICON_PLAY : ICON_PAUSE, "",
        hold ? C_GREEN : C_ORANGE, true);
    add(396, ROW_Y0 + ROW_H, 80, ROW_H - 4, B_STOP, ICON_STOP, "", C_RED, true);
    add(396, ROW_Y0 + 2 * ROW_H, 38, ROW_H - 4, B_FEED_MINUS, -1, "F-", C_BUTTON, true);
    add(438, ROW_Y0 + 2 * ROW_H, 38, ROW_H - 4, B_FEED_PLUS, -1, "F+", C_BUTTON, true);
  } else {
    for (int a = 0; a < 3; a++)
      add(396, ROW_Y0 + a * ROW_H, 80, ROW_H - 4, B_ZERO_X + a, ICON_ZERO, a == 0 ? "X" : a == 1 ? "Y" : "Z",
          C_BUTTON, mv);
    if (grbl.state == ST_ALARM && grbl.connected) {
      add(260, 186, 104, 38, B_UNLOCK, ICON_UNLOCK, "Unlock", C_ORANGE, true);
      add(372, 186, 104, 38, B_HOME, ICON_HOME, "Home", C_BUTTON, true);
    }
  }
}

static void dro_static(void)
{
  for (int a = 0; a < 3; a++) {
    int y = ROW_Y0 + a * ROW_H;
    gfx_inset(4, y, 386, ROW_H - 4, 7, C_PANEL, C_BG);
    char l[2] = {"XYZ"[a], 0};
    plabel(y, ROW_H - 4, C_PANEL, &font_big, 12, y + 1, l, C_ACCENT);
    plabel(y, ROW_H - 4, C_PANEL, &font_small, 306, y + 5, "Machine", C_DIM);
  }
}

static void dro_dynamic(void)
{
  char s[48];
  for (int a = 0; a < 3; a++) {
    int y = ROW_Y0 + a * ROW_H;
    s[0] = grbl.connected ? 'c' : 'd';
    fmt_fixed(s + 1, sizeof(s) - 1, grbl.wpos[a], 3);
    if (changed(2 + a, s))
      ptext(y, ROW_H - 4, C_PANEL, &font_big, 42, y + 1, 256, 44, ALIGN_RIGHT, s + 1, grbl.connected ? C_TEXT : C_DIM);
    fmt_fixed(s, sizeof(s), grbl.mpos[a], 2);
    if (changed(5 + a, s)) ptext(y, ROW_H - 4, C_PANEL, &font_small, 300, y + 23, 84, 18, ALIGN_RIGHT, s, C_DIM);
  }

  // info strip, y 186..224
  const int y = 186, h = 38;
  char key[48];
  bool job = job_active();
  bool alarm = !job && grbl.state == ST_ALARM && grbl.connected;
  snprintf(key, sizeof(key), "%d", job ? 1 : alarm ? 2 : 0);
  if (changed(8, key)) {
    gfx_fill(0, y - 2, LCD_WIDTH, h + 4, C_BG);
    if (job) gfx_inset(4, y, 472, h, 7, C_PANEL, C_BG);
    else if (alarm) gfx_inset(4, y, 250, h, 7, gfx_darken(C_RED, 120), C_BG);
    else gfx_inset(4, y, 472, h, 7, C_PANEL, C_BG);
    for (int i = 9; i < 14; i++) cache[i][0] = (char)0xFF;
    for (int i = 0; i < nbuttons; i++)  // buttons that sit on the strip (alarm Unlock / Home)
      if (buttons[i].y >= y - 2 && buttons[i].y < NAV_Y) draw_button(&buttons[i], false);
  }
  if (job) {
    const char * name = strip_slash(grbl.sd_file);
    snprintf(s, sizeof(s), "%.22s", grbl.sd_running ? name : "Running");
    if (changed(9, s)) ptext(y, h, C_PANEL, &font_small, 12, y + 2, 206, 18, ALIGN_LEFT, s, C_TEXT);
    char p[16];
    fmt_fixed(p, sizeof(p), grbl.sd_pct, 1);
    strcat(p, "%");
    if (changed(10, p)) {
      ptext(y, h, C_PANEL, &font_small, 404, y + 2, 66, 18, ALIGN_RIGHT, p, C_TEXT);
      int bw = 180, fill = (int)(grbl.sd_pct * bw / 100);
      if (fill > bw) fill = bw;
      gfx_vgradient(222, y + 6, fill, 10, gfx_lighten(C_ACCENT, 70), gfx_darken(C_ACCENT, 40));
      gfx_vgradient(222 + fill, y + 6, bw - fill, 10, gfx_darken(C_PANEL, 110), gfx_darken(C_PANEL, 30));
      gfx_fill(222 + fill, y + 6, bw - fill, 1, gfx_darken(C_PANEL, 160));
    }
    snprintf(s, sizeof(s), "Feed %d (%d%%)   Spindle %d (%d%%)", (int)grbl.feed, grbl.ov_feed, (int)grbl.spindle,
             grbl.ov_spindle);
    if (changed(11, s)) ptext(y, h, C_PANEL, &font_small, 12, y + 19, 458, 18, ALIGN_LEFT, s, C_DIM);
  } else if (alarm) {
    if (changed(9, grbl.msg)) {
      uint16_t bg = gfx_darken(C_RED, 120);
      (void)bg;
      ptext(y, h, bg, &font_small, 12, y + 2, 236, 18, ALIGN_LEFT, "Alarm: unlock or home", C_TEXT);
      ptext(y, h, bg, &font_small, 12, y + 19, 236, 17, ALIGN_LEFT, grbl.msg_is_error ? grbl.msg : "", gfx_lighten(C_RED, 120));
    }
  } else {
    snprintf(s, sizeof(s), "Feed %d", (int)grbl.feed);
    if (changed(9, s)) ptext(y, h, C_PANEL, &font_small, 12, y + 10, 120, 18, ALIGN_LEFT, s, C_DIM);
    snprintf(s, sizeof(s), "Spindle %s%d", grbl.spindle_on ? "" : "off ", (int)grbl.spindle);
    if (changed(10, s)) ptext(y, h, C_PANEL, &font_small, 136, y + 10, 160, 18, ALIGN_LEFT, s, C_DIM);
    snprintf(s, sizeof(s), "%s%s%s", grbl.flood ? "Flood " : "", grbl.pins[0] ? "Pin " : "", grbl.pins);
    if (changed(11, s)) ptext(y, h, C_PANEL, &font_small, 300, y + 10, 170, 18, ALIGN_RIGHT, s, C_YELLOW);
  }
}

// ---------------------------------------------------------------- Move
static void move_layout(void)
{
  bool mv = can_move();
  const int s = 60, g = 4, x0 = 6, y0 = TOP;
  add(x0 + s + g, y0, s, s, B_YP, ICON_UP, "", C_BUTTON, mv);
  add(x0, y0 + s + g, s, s, B_XM, ICON_LEFT, "", C_BUTTON, mv);
  add(x0 + s + g, y0 + s + g, s, s, B_JOG_STOP, ICON_STOP, "", C_RED, grbl.state == ST_JOG);
  add(x0 + 2 * (s + g), y0 + s + g, s, s, B_XP, ICON_RIGHT, "", C_BUTTON, mv);
  add(x0 + s + g, y0 + 2 * (s + g), s, s, B_YM, ICON_DOWN, "", C_BUTTON, mv);
  int zx = x0 + 3 * (s + g) + 6;
  add(zx, y0, s, s, B_ZP, ICON_UP, "", C_BUTTON, mv);
  add(zx, y0 + s + g, s, s, B_HOME, ICON_HOME, "", C_BUTTON, mv || grbl.state == ST_ALARM);
  add(zx, y0 + 2 * (s + g), s, s, B_ZM, ICON_DOWN, "", C_BUTTON, mv);

  static const char * sl[4] = {"0.1", "1", "10", "50"};
  for (int i = 0; i < 4; i++) {
    add(276 + i * 50, 96, 46, 34, B_STEP0 + i, -1, sl[i], C_BUTTON, true);
    if (i == step_sel) last()->style = ST_SELECTED;
  }
  static const char * fl[3] = {"300", "1000", "2500"};
  for (int i = 0; i < 3; i++) {
    add(276 + i * 67, 136, 63, 34, B_FEED0 + i, -1, fl[i], C_BUTTON, true);
    if (i == feed_sel) last()->style = ST_SELECTED;
  }
  add(276, 178, 46, 44, B_ZERO_XY, -1, "XY0", C_BUTTON, mv);
  add(326, 178, 46, 44, B_ZERO_Z, -1, "Z0", C_BUTTON, mv);
  add(376, 178, 46, 44, B_GOTO_ZERO, ICON_HOME, "", C_BUTTON, mv);
  add(426, 178, 50, 44, B_PROBE, ICON_PROBE, "", C_BUTTON, mv);
}

static void move_static(void)
{
  gfx_inset(276, TOP, 200, 58, 7, C_PANEL, C_BG);
}

static void move_dynamic(void)
{
  char s[24], v[16];
  for (int a = 0; a < 3; a++) {
    fmt_fixed(v, sizeof(v), grbl.wpos[a], 3);
    snprintf(s, sizeof(s), "%c %10s", "XYZ"[a], v);
    if (changed(2 + a, s)) ptext(TOP, 58, C_PANEL, &font_small, 286, TOP + 2 + a * 18, 180, 18, ALIGN_LEFT, s, C_TEXT);
  }
}

static void jog(int axis, int dir)
{
  char cmd[64], d[16];
  fmt_fixed(d, sizeof(d), steps[step_sel] * dir, 3);
  snprintf(cmd, sizeof(cmd), "$J=G91 G21 %c%s F%d", "XYZ"[axis], d, feeds[feed_sel]);
  grbl_send(cmd);
}

// ---------------------------------------------------------------- Files
#define FILE_ROWS 5
#define FROW_H 38

static void files_layout(void)
{
  for (int r = 0; r < FILE_ROWS && file_top + r < grbl.file_count; r++) {
    add(4, TOP + r * FROW_H, 386, FROW_H - 4, B_ROW0 + r, -1, "", C_BG, true);
    last()->style = ST_HIDDEN;  // drawn as a list row, the button is only the hit area
  }
  add(396, TOP, 80, 44, B_UP, ICON_UP, "", C_BUTTON, file_top > 0);
  add(396, TOP + 48, 80, 44, B_DOWN, ICON_DOWN, "", C_BUTTON, file_top + FILE_ROWS < grbl.file_count);
  add(396, TOP + 96, 80, 44, B_REFRESH, ICON_REFRESH, "", C_BUTTON, grbl.connected && !grbl.files_loading);
  add(396, TOP + 144, 80, 46, B_RUN, ICON_PLAY, "Run", C_GREEN, file_sel >= 0 && can_move());
}

static void draw_file_row(int r)
{
  int i = file_top + r;
  int y = TOP + r * FROW_H;
  gfx_fill(4, y, 386, FROW_H - 2, C_BG);
  if (i >= grbl.file_count) return;
  bool sel = i == file_sel;
  uint16_t base = sel ? C_ACCENT : C_PANEL, t, b;
  if (sel) { gfx_bevel(4, y, 386, FROW_H - 4, 6, base, C_BG, false); gfx_bevel_face(base, false, &t, &b); }
  else { gfx_inset(4, y, 386, FROW_H - 4, 6, base, C_BG); t = gfx_darken(base, 40); b = gfx_lighten(base, 12); }
  uint16_t fg = sel ? C_WHITE : C_TEXT;
  char n[40], sz[16];
  snprintf(n, sizeof(n), "%.30s", strip_slash(grbl.file_name[i]));
  uint32_t bytes = grbl.file_size[i];
  if (bytes >= 1048576) snprintf(sz, sizeof(sz), "%lu.%lu MB", (unsigned long)(bytes / 1048576), (unsigned long)(bytes % 1048576 * 10 / 1048576));
  else snprintf(sz, sizeof(sz), "%lu KB", (unsigned long)((bytes + 1023) / 1024));
  int ty = y + (FROW_H - 4 - font_small.h) / 2;
  gfx_icon(ICON_FILES, 10, y + (FROW_H - 4 - ICON_SIZE) / 2, sel ? C_WHITE : C_DIM, t, b, y, FROW_H - 4);
  gfx_text_on(&font_small, 42, ty, n, fg, t, b, y, FROW_H - 4);
  gfx_text_on(&font_small, 382 - gfx_text_width(&font_small, sz), ty, sz, sel ? C_WHITE : C_DIM, t, b, y, FROW_H - 4);
}

static void files_static(void)
{
  for (int r = 0; r < FILE_ROWS; r++) draw_file_row(r);
}

static void files_dynamic(void)
{
  const char * s = grbl.files_loading ? "Reading SD card..." :
                   !grbl.connected ? "Controller not connected" :
                   grbl.file_count == 0 ? "No files on the controller's SD card" : "";
  if (changed(2, s) && s[0]) gfx_text_box(&font_small, 12, TOP + 80, 370, 20, ALIGN_CENTER, s, C_DIM, C_BG);
}

// ---------------------------------------------------------------- Tool (spindle, coolant)
static void tool_layout(void)
{
  bool ok = grbl.connected && (can_move() || job_active());
  bool idle = can_move();
  add(14, TOP + 30, 84, 44, B_SPIN_OFF, ICON_POWER, "Off", C_RED, idle);
  static const char * pl[4] = {"25%", "50%", "75%", "100%"};
  for (int i = 0; i < 4; i++) {
    add(106 + i * 92, TOP + 30, 86, 44, B_SPIN0 + i, ICON_SPINDLE, pl[i], C_BUTTON, idle);
    if (grbl.spindle_on && (i + 1) * 25 == spin_pct) last()->style = ST_SELECTED;
  }
  add(14, TOP + 130, 104, 44, B_FLOOD_ON, ICON_COOLANT, "On", grbl.flood ? C_ACCENT : C_BUTTON, ok);
  add(124, TOP + 130, 104, 44, B_FLOOD_OFF, -1, "Off", C_BUTTON, ok);
  add(252, TOP + 130, 70, 44, B_SOVR_MINUS, -1, "-10", C_BUTTON, ok);
  add(326, TOP + 130, 70, 44, B_SOVR_100, -1, "100", C_BUTTON, ok);
  add(400, TOP + 130, 70, 44, B_SOVR_PLUS, -1, "+10", C_BUTTON, ok);
}

static void tool_static(void)
{
  gfx_inset(4, TOP, 472, 86, 8, C_PANEL, C_BG);
  plabel(TOP, 86, C_PANEL, &font_small, 14, TOP + 6, "Spindle", C_TEXT);
  gfx_inset(4, TOP + 100, 232, 86, 8, C_PANEL, C_BG);
  plabel(TOP + 100, 86, C_PANEL, &font_small, 14, TOP + 106, "Coolant (flood)", C_TEXT);
  gfx_inset(244, TOP + 100, 232, 86, 8, C_PANEL, C_BG);
  plabel(TOP + 100, 86, C_PANEL, &font_small, 254, TOP + 106, "Spindle override", C_TEXT);
}

static void tool_dynamic(void)
{
  char s[48];
  if (grbl.spindle_max > 0)
    snprintf(s, sizeof(s), "%s  S%d of %d", grbl.spindle_on ? (grbl.spindle_ccw ? "CCW" : "ON") : "OFF",
             (int)grbl.spindle, (int)grbl.spindle_max);
  else
    snprintf(s, sizeof(s), "%s  S%d", grbl.spindle_on ? "ON" : "OFF", (int)grbl.spindle);
  if (changed(2, s)) ptext(TOP, 86, C_PANEL, &font_small, 120, TOP + 6, 350, 18, ALIGN_RIGHT, s, grbl.spindle_on ? C_YELLOW : C_DIM);
  snprintf(s, sizeof(s), "%d%%", grbl.ov_spindle);
  if (changed(3, s)) ptext(TOP + 100, 86, C_PANEL, &font_small, 420, TOP + 106, 50, 18, ALIGN_RIGHT, s, C_DIM);
  if (changed(4, grbl.flood ? "on" : "off"))
    ptext(TOP + 100, 86, C_PANEL, &font_small, 170, TOP + 106, 60, 18, ALIGN_RIGHT, grbl.flood ? "on" : "off", grbl.flood ? C_ACCENT : C_DIM);
}

static void spindle_on(int pct)
{
  float max = grbl.spindle_max > 0 ? grbl.spindle_max : 1000;  // DLC32 default $30=1000
  char cmd[32];
  spin_pct = pct;
  snprintf(cmd, sizeof(cmd), "M3 S%d", (int)(max * pct / 100 + 0.5f));
  grbl_send(cmd);
}

// ---------------------------------------------------------------- Code (G-code console)
#define LOG_ROWS 7
#define LOG_Y    (TOP)
#define LOG_ROW_H 19

static void code_layout(void)
{
  add(396, 186, 38, 38, B_KEYBOARD, ICON_KEYBOARD, "", C_BUTTON, grbl.connected);
  add(438, 186, 38, 38, B_CLEAR_LOG, ICON_TRASH, "", C_BUTTON, true);
}

static void code_static(void)
{
  gfx_inset(4, LOG_Y, 472, LOG_ROWS * LOG_ROW_H + 10, 7, C_CONSOLE, C_BG);
  gfx_inset(4, 186, 386, 38, 7, C_PANEL, C_BG);
}

static void code_dynamic(void)
{
  if (grbl_log_seq != log_seq_seen) {
    log_seq_seen = grbl_log_seq;
    for (int r = 0; r < LOG_ROWS; r++) {
      int idx = (grbl_log_head - LOG_ROWS + r + LOG_LINES) % LOG_LINES;
      log_line_t * l = &grbl_log[idx];
      char s[56];
      uint16_t c = l->kind == LOG_TX ? C_ACCENT : l->kind == LOG_ERR ? gfx_lighten(C_RED, 50) : C_TEXT;
      snprintf(s, sizeof(s), "%s%.48s", l->text[0] ? (l->kind == LOG_TX ? "> " : "< ") : "", l->text);
      ptext(LOG_Y, LOG_ROWS * LOG_ROW_H + 10, C_CONSOLE, &font_small, 12, LOG_Y + 5 + r * LOG_ROW_H, 458, LOG_ROW_H, ALIGN_LEFT, s, c);
    }
  }
  char s[64], pct[16];
  fmt_fixed(pct, sizeof(pct), grbl.sd_pct, 1);
  if (grbl.sd_line > 0) snprintf(s, sizeof(s), "Ln %ld: %.30s", grbl.sd_line, grbl.sd_line_text);
  else if (grbl.sd_running) snprintf(s, sizeof(s), "Job %s%%  (line not reported)", pct);
  else snprintf(s, sizeof(s), "No job running");
  if (changed(2, s)) ptext(186, 38, C_PANEL, &font_small, 12, 196, 372, 18, ALIGN_LEFT, s, grbl.sd_line > 0 ? C_YELLOW : C_DIM);
}

// ---------------------------------------------------------------- Setup and WiFi
static void setup_layout(void)
{
  add(4, TOP, 232, 44, B_CALIBRATE, ICON_DRO, "Calibrate touch", C_BUTTON, true);
  add(244, TOP, 232, 44, B_WIFI, ICON_WIFI, "WiFi", C_BUTTON, grbl.connected);
  add(4, TOP + 52, 232, 44, B_BUZZER, -1, buzzer_on ? "Key sound: on" : "Key sound: off", C_BUTTON, true);
  add(244, TOP + 52, 232, 44, B_RESET, ICON_POWER, "Reset controller", C_RED, grbl.connected);
}

static void setup_static(void)
{
  gfx_inset(4, TOP + 106, 472, 84, 8, C_PANEL, C_BG);
  plabel(TOP + 106, 84, C_PANEL, &font_small, 14, TOP + 112, "About", C_TEXT);
  plabel(TOP + 106, 84, C_PANEL, &font_small, 14, TOP + 134, "Screen      TFT43 Grbl v" FW_VERSION, C_DIM);
}

static void setup_dynamic(void)
{
  char s[48];
  snprintf(s, sizeof(s), "Controller  %.24s", grbl.version[0] ? grbl.version : grbl.connected ? "Grbl" : "not connected");
  if (changed(2, s)) ptext(TOP + 106, 84, C_PANEL, &font_small, 14, TOP + 154, 450, 18, ALIGN_LEFT, s, C_DIM);
}

static void wifi_layout(void)
{
  add(4, TOP, 300, 44, B_SSID, ICON_WIFI, "Network name", C_BUTTON, true);
  add(4, TOP + 50, 300, 44, B_PASS, ICON_KEYBOARD, "Password", C_BUTTON, true);
  add(312, TOP, 164, 44, B_GET_IP, ICON_REFRESH, "Show IP", C_BUTTON, grbl.connected);
  add(312, TOP + 50, 164, 44, B_CONNECT, ICON_PLAY, "Connect", C_GREEN, grbl.connected && wifi_ssid[0]);
  add(4, 182, 110, 42, B_BACK, ICON_BACK, "Back", C_BUTTON, true);
}

static void wifi_static(void)
{
  gfx_inset(4, TOP + 100, 472, 44, 8, C_PANEL, C_BG);
  gfx_text(&font_small, 122, 194, "Connect saves the network and", C_DIM, C_BG);
  gfx_text(&font_small, 122, 210, "restarts the controller", C_DIM, C_BG);
}

static void wifi_dynamic(void)
{
  char s[64], stars[24];
  size_t n = strlen(wifi_pass);
  memset(stars, '*', n < sizeof(stars) - 1 ? n : sizeof(stars) - 1);
  stars[n < sizeof(stars) - 1 ? n : sizeof(stars) - 1] = 0;
  snprintf(s, sizeof(s), "%.20s / %s", wifi_ssid[0] ? wifi_ssid : "(no network)", n ? stars : "(no password)");
  if (changed(2, s)) ptext(TOP + 100, 44, C_PANEL, &font_small, 14, TOP + 104, 456, 18, ALIGN_LEFT, s, C_TEXT);
  snprintf(s, sizeof(s), "IP address: %s", grbl.ip[0] ? grbl.ip : "tap Show IP");
  if (changed(3, s)) ptext(TOP + 100, 44, C_PANEL, &font_small, 14, TOP + 122, 456, 18, ALIGN_LEFT, s, C_DIM);
}

// ---------------------------------------------------------------- overlays
static void confirm_layout(void)
{
  add(110, 166, 120, 44, B_CANCEL, -1, "Cancel", C_BUTTON, true);
  add(250, 166, 120, 44, B_OK, -1, "OK", confirm_color, true);
}

static void confirm_static(void)
{
  // dim the screen behind the dialog with a dark frame, then the card
  gfx_fill(0, BAR_H + 1, LCD_WIDTH, LCD_HEIGHT - BAR_H - 1, gfx_darken(C_BG, 80));
  gfx_fill(64, 66, 360, 162, gfx_darken(C_BG, 200));  // shadow
  gfx_bevel(60, 60, 360, 162, 12, C_PANEL, gfx_darken(C_BG, 80), false);
  uint16_t t, b;
  gfx_bevel_face(C_PANEL, false, &t, &b);
  int tw = gfx_text_width(&font_medium, confirm_title);
  gfx_text_on(&font_medium, 240 - tw / 2, 76, confirm_title, C_TEXT, t, b, 60, 162);
  char s[48];
  snprintf(s, sizeof(s), "%.38s", confirm_text);
  tw = gfx_text_width(&font_small, s);
  gfx_text_on(&font_small, 240 - tw / 2, 116, s, C_DIM, t, b, 60, 162);
}

// keyboard: 4 rows of 10 + a control row
static const char * kb_rows[2][4] = {
  {"1234567890", "QWERTYUIOP", "ASDFGHJKL-", "ZXCVBNM.=$"},
  {"1234567890", "!@#%^&*()_", "[]{};:'\"/?", "<>,+~|\\`#$"},
};
enum { K_SHIFT = 40, K_SYM, K_SPACE, K_DEL, K_CANCEL, K_SEND };

static char kb_char(int row, int col)
{
  char c = kb_rows[kb_sym][row][col];
  if (!kb_sym && !kb_shift && c >= 'A' && c <= 'Z') c = c - 'A' + 'a';
  return c;
}

static void keyboard_layout(void)
{
  for (int r = 0; r < 4; r++)
    for (int c = 0; c < 10; c++) {
      char l[2] = {kb_char(r, c), 0};
      add(4 + c * 47, 46 + r * 44, 43, 40, B_KEY + r * 10 + c, -1, l, C_BUTTON, true);
    }
  add(4, 226, 66, 42, B_KEY + K_SHIFT, ICON_UP, "", kb_shift ? C_ACCENT : C_BUTTON, !kb_sym);
  add(74, 226, 66, 42, B_KEY + K_SYM, -1, kb_sym ? "ABC" : "#+=", C_BUTTON, true);
  add(144, 226, 120, 42, B_KEY + K_SPACE, -1, "Space", C_BUTTON, true);
  add(268, 226, 66, 42, B_KEY + K_DEL, ICON_BACK, "", C_BUTTON, true);
  add(338, 226, 66, 42, B_KEY + K_CANCEL, -1, "Esc", C_BUTTON, true);
  add(408, 226, 68, 42, B_KEY + K_SEND, -1, kb_target == KB_GCODE ? "Send" : "OK", C_GREEN, true);
}

static void keyboard_static(void)
{
  gfx_fill(0, 0, LCD_WIDTH, LCD_HEIGHT, C_BG);
}

static void keyboard_dynamic(void)
{
  char shown[sizeof(kb_buf) + 1];
  int len = (int)strlen(kb_buf);
  for (int i = 0; i < len; i++)  // password: show only the last character typed
    shown[i] = (kb_target == KB_PASS && i < len - 1) ? '*' : kb_buf[i];
  shown[len] = 0;
  const char * tail = len > 20 ? shown + len - 20 : shown;
  const char * title = kb_target == KB_GCODE ? "G-code" : kb_target == KB_SSID ? "Network" : "Password";
  char s[48];
  snprintf(s, sizeof(s), "%d|%d|%.24s", (int)kb_target, len, tail);
  if (changed(2, s)) {
    gfx_inset(4, 4, 472, 36, 7, C_CONSOLE, C_BG);
    plabel(4, 36, C_CONSOLE, &font_small, 14, 13, title, C_DIM);
    char v[32];
    snprintf(v, sizeof(v), "%.27s_", len > 27 ? shown + len - 27 : shown);
    ptext(4, 36, C_CONSOLE, &font_medium, 100, 9, 368, 26, ALIGN_LEFT, v, C_TEXT);
  }
}

static void keyboard_done(void)
{
  switch (kb_target) {
    case KB_GCODE: if (kb_buf[0]) grbl_send(kb_buf); break;
    case KB_SSID: strncpy(wifi_ssid, kb_buf, sizeof(wifi_ssid) - 1); break;
    case KB_PASS: strncpy(wifi_pass, kb_buf, sizeof(wifi_pass) - 1); break;
  }
  overlay = OV_NONE;
}

static void key(int k)
{
  size_t n = strlen(kb_buf);
  if (k < 40) {
    if (n < sizeof(kb_buf) - 1) { kb_buf[n] = kb_char(k / 10, k % 10); kb_buf[n + 1] = 0; }
    if (kb_shift && kb_target != KB_GCODE && !kb_sym) kb_shift = false;  // one-shot shift for text
    return;
  }
  switch (k) {
    case K_SHIFT: kb_shift = !kb_shift; break;
    case K_SYM: kb_sym = !kb_sym; break;
    case K_SPACE: if (n < sizeof(kb_buf) - 1) { kb_buf[n] = ' '; kb_buf[n + 1] = 0; } break;
    case K_DEL: if (n) kb_buf[n - 1] = 0; break;
    case K_CANCEL: overlay = OV_NONE; break;
    case K_SEND: keyboard_done(); break;
  }
}

// ---------------------------------------------------------------- actions
static void zero_axes(const char * axes)
{
  char cmd[40];
  snprintf(cmd, sizeof(cmd), "G10 L20 P0 %s", axes);
  grbl_send(cmd);
}

static void confirmed(uint8_t action)
{
  switch (action) {
    case B_RUN: {
      char cmd[80];
      snprintf(cmd, sizeof(cmd), "$SD/Run=/%s", strip_slash(grbl.file_name[file_sel]));
      grbl_send(cmd);
      ui_show(SCR_DRO);
      break;
    }
    case B_PROBE:
      // MKS "Knife" probe: probe down up to 50 mm, set Z0 at the plate, lift 5 mm
      grbl_send("G38.2 G91 Z-50 F80");
      grbl_send("G10 L20 P0 Z0");
      grbl_send("G91 G0 Z5");
      grbl_send("G90");
      break;
    case B_GOTO_ZERO: grbl_send("G90 G0 Z5"); grbl_send("G90 G0 X0 Y0"); break;
    case B_RESET: grbl_realtime(RT_RESET); break;
    case B_STOP:
      grbl_realtime(RT_HOLD);  // decelerate first, reset once at rest (see ui_step)
      stop_pending = true;
      stop_at = millis();
      break;
    case B_CONNECT: {
      char cmd[128];
      snprintf(cmd, sizeof(cmd), "[ESP100]%s", wifi_ssid); grbl_send(cmd);
      snprintf(cmd, sizeof(cmd), "[ESP101]%s", wifi_pass); grbl_send(cmd);
      grbl_send("[ESP110]STA");
      grbl_send("[ESP444]RESTART");
      break;
    }
  }
}

static void on_button(uint8_t id)
{
  if (id >= B_KEY) { key(id - B_KEY); return; }
  if (id >= B_NAV0 && id <= B_NAV5) {
    screen_t s = (screen_t)(id - B_NAV0);
    ui_show(s);
    if (s == SCR_FILES && grbl.file_count == 0 && grbl.connected) grbl_send("$SD/List");
    if (s == SCR_TOOL && grbl.spindle_max == 0 && can_move()) grbl_send("$$");
    return;
  }
  switch (id) {
    case B_ZERO_X: zero_axes("X0"); break;
    case B_ZERO_Y: zero_axes("Y0"); break;
    case B_ZERO_Z: zero_axes("Z0"); break;
    case B_ZERO_XY: zero_axes("X0 Y0"); break;
    case B_UNLOCK: grbl_send("$X"); break;
    case B_HOME: grbl_send("$H"); break;
    case B_PAUSE: grbl_realtime(RT_HOLD); break;
    case B_RESUME: grbl_realtime(RT_RESUME); break;
    case B_STOP: ask("Stop the job?", "The spindle stops and the job ends", B_STOP, C_RED); break;
    case B_FEED_MINUS: grbl_realtime(RT_FEED_MINUS); break;
    case B_FEED_PLUS: grbl_realtime(RT_FEED_PLUS); break;
    case B_XM: jog(0, -1); break;
    case B_XP: jog(0, 1); break;
    case B_YM: jog(1, -1); break;
    case B_YP: jog(1, 1); break;
    case B_ZM: jog(2, -1); break;
    case B_ZP: jog(2, 1); break;
    case B_JOG_STOP: grbl_realtime(RT_JOG_CANCEL); break;
    case B_STEP0: case B_STEP1: case B_STEP2: case B_STEP3: step_sel = id - B_STEP0; break;
    case B_FEED0: case B_FEED1: case B_FEED2: feed_sel = id - B_FEED0; break;
    case B_GOTO_ZERO: ask("Go to XY zero?", "Lifts Z to 5 mm, then moves to X0 Y0", B_GOTO_ZERO, C_GREEN); break;
    case B_PROBE: ask("Probe Z?", "Plate under the tool, clip attached", B_PROBE, C_GREEN); break;
    case B_UP: file_top -= FILE_ROWS; if (file_top < 0) file_top = 0; break;
    case B_DOWN: file_top += FILE_ROWS; break;
    case B_REFRESH: file_sel = -1; file_top = 0; grbl_send("$SD/List"); break;
    case B_ROW0: case B_ROW1: case B_ROW2: case B_ROW3: case B_ROW4: {
      int i = file_top + (id - B_ROW0);
      file_sel = (file_sel == i) ? -1 : i;
      break;
    }
    case B_RUN:
      if (file_sel >= 0) {
        char t[48];
        snprintf(t, sizeof(t), "%.38s", strip_slash(grbl.file_name[file_sel]));
        ask("Run this file?", t, B_RUN, C_GREEN);
      }
      break;
    case B_SPIN_OFF: grbl_send("M5"); break;
    case B_SPIN0: case B_SPIN1: case B_SPIN2: case B_SPIN3: spindle_on((id - B_SPIN0 + 1) * 25); break;
    case B_FLOOD_ON: grbl_send("M8"); break;
    case B_FLOOD_OFF: grbl_send("M9"); break;
    case B_SOVR_MINUS: grbl_realtime(RT_SPIN_MINUS); break;
    case B_SOVR_PLUS: grbl_realtime(RT_SPIN_PLUS); break;
    case B_SOVR_100: grbl_realtime(RT_SPIN_100); break;
    case B_KEYBOARD: open_keyboard(KB_GCODE, ""); break;
    case B_CLEAR_LOG: grbl_log_clear(); break;
    case B_CALIBRATE: ui_request_calibration = true; break;
    case B_WIFI: ui_show(SCR_WIFI); if (!grbl.ip[0]) grbl_send("[ESP111]"); break;
    case B_BUZZER: buzzer_on = !buzzer_on; break;
    case B_RESET: ask("Reset the controller?", "Stops any motion and clears the queue", B_RESET, C_RED); break;
    case B_SSID: open_keyboard(KB_SSID, wifi_ssid); break;
    case B_PASS: open_keyboard(KB_PASS, wifi_pass); break;
    case B_GET_IP: grbl.ip[0] = 0; grbl_send("[ESP111]"); break;
    case B_CONNECT: ask("Join this network?", wifi_ssid, B_CONNECT, C_GREEN); break;
    case B_BACK: ui_show(SCR_SETUP); break;
    case B_OK: overlay = OV_NONE; confirmed(confirm_action); break;
    case B_CANCEL: overlay = OV_NONE; break;
  }
}

// ---------------------------------------------------------------- framework
static uint32_t make_key(void)
{
  uint32_t k = screen;
  uint32_t v[] = {overlay, grbl.connected, grbl.state, job_active(), step_sel, feed_sel, (uint32_t)file_top,
                  (uint32_t)(file_sel + 1), grbl.files_loading, grbl.files_seq, grbl.spindle_on, grbl.flood,
                  (uint32_t)spin_pct, buzzer_on, kb_shift, kb_sym, (uint32_t)kb_target, wifi_ssid[0] != 0};
  for (unsigned i = 0; i < sizeof(v) / sizeof(v[0]); i++) k = k * 31 + v[i];
  return k;
}

static void build_layout(void)
{
  nbuttons = 0;
  if (overlay == OV_KEYBOARD) { keyboard_layout(); return; }
  if (overlay == OV_CONFIRM) { confirm_layout(); return; }
  switch (screen) {
    case SCR_DRO: dro_layout(); break;
    case SCR_MOVE: move_layout(); break;
    case SCR_FILES: files_layout(); break;
    case SCR_TOOL: tool_layout(); break;
    case SCR_CONSOLE: code_layout(); break;
    case SCR_SETUP: setup_layout(); break;
    case SCR_WIFI: wifi_layout(); break;
  }
  nav_layout();
}

static void draw_static(void)
{
  if (overlay == OV_KEYBOARD) { keyboard_static(); return; }
  if (overlay == OV_CONFIRM) { confirm_static(); return; }
  switch (screen) {
    case SCR_DRO: dro_static(); break;
    case SCR_MOVE: move_static(); break;
    case SCR_FILES: files_static(); break;
    case SCR_TOOL: tool_static(); break;
    case SCR_CONSOLE: code_static(); break;
    case SCR_SETUP: setup_static(); break;
    case SCR_WIFI: wifi_static(); break;
  }
}

static void redraw(void)
{
  bool full = drawn_screen != (int)screen || drawn_overlay != (int)overlay;
  if (full) {
    if (overlay != OV_CONFIRM) {
      bar_static();
      gfx_fill(0, BAR_H + 1, LCD_WIDTH, LCD_HEIGHT - BAR_H - 1, C_BG);
    }
    invalidate();
    drawn_screen = screen;
    drawn_overlay = overlay;
    draw_static();
  } else if (overlay == OV_NONE && screen == SCR_FILES) {
    files_static();  // selection or page changed
  }
  build_layout();
  // clear areas of buttons that went away: redraw the content background under all buttons first
  if (!full && overlay == OV_NONE && screen == SCR_DRO) {
    gfx_fill(394, ROW_Y0 - 1, 86, 3 * ROW_H + 2, C_BG);
    cache[8][0] = (char)0xFF;  // info strip
  }
  for (int i = 0; i < nbuttons; i++) draw_button(&buttons[i], false);
  pressed = -1;
}

void ui_show(screen_t s)
{
  screen = s;
  overlay = OV_NONE;
  layout_key = 0xFFFFFFFF;
  drawn_screen = -1;
}

void ui_init(void)
{
  grbl_init();
  ui_show(SCR_DRO);
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
  if (stop_pending) {
    bool rested = !strncmp(grbl.state_text, "Hold:0", 6) || grbl.state == ST_IDLE;
    if (rested || millis() - stop_at > 5000) {
      grbl_realtime(RT_RESET);
      stop_pending = false;
    }
  }
  if (grbl.files_seq != files_seq_seen) {
    files_seq_seen = grbl.files_seq;
    file_sel = -1;
    file_top = 0;
  }
  if (file_top >= grbl.file_count) file_top = 0;
  if (overlay == OV_CONFIRM && confirm_action == B_RUN && !can_move()) overlay = OV_NONE;

  uint32_t k = make_key();
  if (k != layout_key && pressed < 0) {
    layout_key = k;
    redraw();
  }

  static bool was_down;
  static int last_x, last_y;
  if (down) {
    last_x = x; last_y = y;
    if (!was_down) {
      int h = hit(x, y);
      if (h >= 0 && buttons[h].enabled) {
        pressed = h;
        draw_button(&buttons[h], true);
        if (buzzer_on) beep(30);
      }
    }
  } else if (was_down && pressed >= 0) {
    button_t b = buttons[pressed];
    bool inside = hit(last_x, last_y) == pressed;
    draw_button(&buttons[pressed], false);
    pressed = -1;
    if (inside) on_button(b.id);
    layout_key = 0xFFFFFFFF;
  }
  was_down = down;

  if (overlay == OV_KEYBOARD) { keyboard_dynamic(); return; }
  if (overlay == OV_CONFIRM) return;
  draw_bar();
  switch (screen) {
    case SCR_DRO: dro_dynamic(); break;
    case SCR_MOVE: move_dynamic(); break;
    case SCR_FILES: files_dynamic(); break;
    case SCR_TOOL: tool_dynamic(); break;
    case SCR_CONSOLE: code_dynamic(); break;
    case SCR_SETUP: setup_dynamic(); break;
    case SCR_WIFI: wifi_dynamic(); break;
  }
}
