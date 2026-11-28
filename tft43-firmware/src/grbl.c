// Grbl 1.1 / Grbl_Esp32 serial protocol client.
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include "hw.h"
#include "uart.h"
#include "grbl.h"

grbl_t grbl;
log_line_t grbl_log[LOG_LINES];
int grbl_log_head;
uint32_t grbl_log_seq;

static void log_add(uint8_t kind, const char * text)
{
  log_line_t * l = &grbl_log[grbl_log_head];
  l->kind = kind;
  strncpy(l->text, text, LOG_LEN - 1);
  l->text[LOG_LEN - 1] = 0;
  grbl_log_head = (grbl_log_head + 1) % LOG_LINES;
  grbl_log_seq++;
}

void grbl_log_clear(void)
{
  memset(grbl_log, 0, sizeof(grbl_log));
  grbl_log_head = 0;
  grbl_log_seq++;
}

#define POLL_MS        200   // status report rate while connected
#define POLL_SLOW_MS   1000  // while disconnected
#define LINK_TIMEOUT   2000  // no reply for this long = disconnected
#define OK_TIMEOUT     15000 // give up waiting for "ok"
#define QLEN 8
#define QLINE 96

static void log_add(uint8_t kind, const char * text);
static char queue[QLEN][QLINE];
static int qh, qt;
static bool waiting_ok;
static uint32_t sent_at, last_poll, last_rx;
static char rxline[256];
static int rxlen;

void grbl_init(void)
{
  memset(&grbl, 0, sizeof(grbl));
  grbl.ov_feed = grbl.ov_rapid = grbl.ov_spindle = 100;
  strcpy(grbl.state_text, "----");
}

bool grbl_send(const char * line)
{
  int n = (qh + 1) % QLEN;
  if (n == qt) return false;
  strncpy(queue[qh], line, QLINE - 1);
  queue[qh][QLINE - 1] = 0;
  qh = n;
  return true;
}

bool grbl_idle_queue(void) { return qh == qt && !waiting_ok; }

void grbl_realtime(uint8_t c)
{
  uart_putc(c);
  const char * name = c == RT_HOLD ? "[feed hold]" : c == RT_RESUME ? "[resume]" : c == RT_RESET ? "[reset]"
                    : c == RT_JOG_CANCEL ? "[jog cancel]" : NULL;
  if (name) log_add(LOG_TX, name);
}

static void set_msg(const char * s, bool err)
{
  strncpy(grbl.msg, s, sizeof(grbl.msg) - 1);
  grbl.msg[sizeof(grbl.msg) - 1] = 0;
  grbl.msg_is_error = err;
  grbl.msg_seq++;
}

// ---- parsing helpers ----
static float parse_float(const char ** p)
{
  const char * s = *p;
  bool neg = false;
  if (*s == '-') { neg = true; s++; } else if (*s == '+') s++;
  float v = 0;
  while (*s >= '0' && *s <= '9') v = v * 10 + (*s++ - '0');
  if (*s == '.') {
    s++;
    float f = 0.1f;
    while (*s >= '0' && *s <= '9') { v += (*s++ - '0') * f; f *= 0.1f; }
  }
  *p = s;
  return neg ? -v : v;
}

static int parse_floats(const char * s, float * out, int max)
{
  int n = 0;
  while (n < max) {
    out[n++] = parse_float(&s);
    if (*s != ',') break;
    s++;
  }
  return n;
}

static grbl_state_t state_from(const char * s)
{
  static const struct { const char * name; grbl_state_t st; } map[] = {
    {"Idle", ST_IDLE}, {"Run", ST_RUN}, {"Hold", ST_HOLD}, {"Jog", ST_JOG}, {"Alarm", ST_ALARM},
    {"Door", ST_DOOR}, {"Check", ST_CHECK}, {"Home", ST_HOME}, {"Sleep", ST_SLEEP},
  };
  for (unsigned i = 0; i < sizeof(map) / sizeof(map[0]); i++)
    if (strncmp(s, map[i].name, strlen(map[i].name)) == 0) return map[i].st;
  return ST_UNKNOWN;
}

// <Idle|MPos:0.000,0.000,0.000|FS:0,0|WCO:0.000,0.000,0.000|Ov:100,100,100|SD:12.50,/file.nc>
static void parse_status(char * s)
{
  char * end = strchr(s, '>');
  if (end) *end = 0;
  bool have_m = false, have_w = false, have_sd = false, have_a = false, have_ln = false;
  char * field = s + 1;
  bool first = true;
  while (field && *field) {
    char * next = strchr(field, '|');
    if (next) *next++ = 0;
    if (first) {
      strncpy(grbl.state_text, field, sizeof(grbl.state_text) - 1);
      grbl.state = state_from(field);
      first = false;
    } else if (!strncmp(field, "MPos:", 5)) {
      parse_floats(field + 5, grbl.mpos, 3); have_m = true;
    } else if (!strncmp(field, "WPos:", 5)) {
      parse_floats(field + 5, grbl.wpos, 3); have_w = true;
    } else if (!strncmp(field, "WCO:", 4)) {
      parse_floats(field + 4, grbl.wco, 3);
    } else if (!strncmp(field, "FS:", 3)) {
      float v[2] = {0, 0};
      parse_floats(field + 3, v, 2);
      grbl.feed = v[0]; grbl.spindle = v[1];
    } else if (!strncmp(field, "F:", 2)) {
      const char * p = field + 2;
      grbl.feed = parse_float(&p);
    } else if (!strncmp(field, "Ov:", 3)) {
      float v[3] = {100, 100, 100};
      parse_floats(field + 3, v, 3);
      grbl.ov_feed = (int)v[0]; grbl.ov_rapid = (int)v[1]; grbl.ov_spindle = (int)v[2];
    } else if (!strncmp(field, "Pn:", 3)) {
      strncpy(grbl.pins, field + 3, sizeof(grbl.pins) - 1);
    } else if (!strncmp(field, "A:", 2)) {
      const char * a = field + 2;
      grbl.spindle_on = strchr(a, 'S') || strchr(a, 'C');
      grbl.spindle_ccw = strchr(a, 'C') != NULL;
      grbl.flood = strchr(a, 'F') != NULL;
      grbl.mist = strchr(a, 'M') != NULL;
      have_a = true;
    } else if (!strncmp(field, "SDLn:", 5)) {
      const char * p = field + 5;
      grbl.sd_line = strtol(p, (char **)&p, 10);
      if (*p == ':') p++;
      strncpy(grbl.sd_line_text, p, sizeof(grbl.sd_line_text) - 1);
      have_ln = true;
    } else if (!strncmp(field, "SD:", 3)) {
      const char * p = field + 3;
      grbl.sd_pct = parse_float(&p);
      if (*p == ',') p++;
      strncpy(grbl.sd_file, p, NAME_LEN - 1);
      have_sd = true;
    }
    field = next;
  }
  // Pn: is only present while a pin is active
  if (!strstr(s, "|Pn:") && !strncmp(s, "<", 1)) grbl.pins[0] = 0;
  for (int i = 0; i < 3; i++) {
    if (have_m) grbl.wpos[i] = grbl.mpos[i] - grbl.wco[i];
    else if (have_w) grbl.mpos[i] = grbl.wpos[i] + grbl.wco[i];
  }
  if (!have_a) grbl.spindle_on = grbl.spindle_ccw = grbl.flood = grbl.mist = false;
  if (!have_ln) { grbl.sd_line = 0; grbl.sd_line_text[0] = 0; }
  grbl.sd_running = have_sd;
  if (!have_sd) grbl.sd_pct = 0;
}

static void parse_file(char * s)  // [FILE:/name.nc|SIZE:1234]
{
  if (grbl.file_count >= MAX_FILES) return;
  char * name = s + 6;
  char * bar = strchr(name, '|');
  uint32_t size = 0;
  if (bar) {
    *bar = 0;
    char * sz = strstr(bar + 1, "SIZE:");
    if (sz) size = strtoul(sz + 5, NULL, 10);
  } else {
    char * e = strchr(name, ']');
    if (e) *e = 0;
  }
  int i = grbl.file_count++;
  strncpy(grbl.file_name[i], name, NAME_LEN - 1);
  grbl.file_name[i][NAME_LEN - 1] = 0;
  grbl.file_size[i] = size;
}

static void reply_done(void)
{
  waiting_ok = false;
  if (grbl.files_loading) { grbl.files_loading = false; grbl.files_seq++; }
}

void grbl_handle_line(char * s)
{
  last_rx = millis();
  if (!grbl.connected) { grbl.connected = true; set_msg("Connected", false); }

  if (s[0] == '<') {
    parse_status(s);
    return;
  }
  if (!strcmp(s, "ok")) {
    reply_done();
    return;
  }
  bool is_err = !strncmp(s, "error", 5) || !strncmp(s, "ALARM:", 6);
  log_add(is_err ? LOG_ERR : LOG_RX, s);

  if (!strncmp(s, "$30=", 4)) {
    grbl.spindle_max = (float)atof(s + 4);
  } else if (isdigit((unsigned char)s[0]) && strchr(s, '.') && strlen(s) < sizeof(grbl.ip) &&
             strspn(s, "0123456789.") == strlen(s)) {
    strcpy(grbl.ip, s);  // reply to [ESP111]
  } else if (!strncmp(s, "error", 5)) {
    // "error:N" (Grbl) or "error N" (Grbl_Esp32 busy reply)
    int code = atoi(s + 6);
    char m[64];
    snprintf(m, sizeof(m), "Error %d: %s", code, grbl_error_text(code));
    set_msg(m, true);
    reply_done();
  } else if (!strncmp(s, "ALARM:", 6)) {
    int code = atoi(s + 6);
    char m[64];
    snprintf(m, sizeof(m), "Alarm %d: %s", code, grbl_alarm_text(code));
    set_msg(m, true);
    grbl.state = ST_ALARM;
  } else if (!strncmp(s, "[FILE:", 6)) {
    parse_file(s);
  } else if (!strncmp(s, "[MSG:", 5)) {
    char * e = strrchr(s, ']');
    if (e) *e = 0;
    set_msg(s + 5, false);
  } else if (!strncmp(s, "Grbl ", 5)) {
    // controller (re)started: drop anything waiting for an answer
    strncpy(grbl.version, s, sizeof(grbl.version) - 1);
    qh = qt = 0;
    waiting_ok = false;
    grbl.files_loading = false;
    set_msg(s, false);
  }
}

static void request_status(uint32_t now)
{
  uint32_t period = grbl.connected ? POLL_MS : POLL_SLOW_MS;
  if (now - last_poll >= period) {
    last_poll = now;
    uart_putc(RT_STATUS);
  }
}

void grbl_poll(void)
{
  int c;
  while ((c = uart_getc()) >= 0) {
    if (c == '\n' || c == '\r') {
      if (rxlen) { rxline[rxlen] = 0; grbl_handle_line(rxline); rxlen = 0; }
    } else if (rxlen < (int)sizeof(rxline) - 1) {
      rxline[rxlen++] = (char)c;
    }
  }

  uint32_t now = millis();
  if (grbl.connected && now - last_rx > LINK_TIMEOUT) {
    grbl.connected = false;
    grbl.state = ST_UNKNOWN;
    strcpy(grbl.state_text, "----");
    waiting_ok = false;
    qh = qt = 0;  // never replay stale commands (e.g. jogs) when the link comes back
    set_msg("No reply from controller", true);
  }
  if (waiting_ok && now - sent_at > OK_TIMEOUT) reply_done();

  if (!waiting_ok && qt != qh && grbl.connected) {
    if (!strncmp(queue[qt], "$SD/List", 8)) { grbl.file_count = 0; grbl.files_loading = true; }
    uart_puts(queue[qt]);
    uart_puts("\n");
    log_add(LOG_TX, queue[qt]);
    qt = (qt + 1) % QLEN;
    waiting_ok = true;
    sent_at = now;
  }
  request_status(now);
}

const char * grbl_alarm_text(int code)
{
  switch (code) {
    case 1: return "Hard limit";
    case 2: return "Soft limit";
    case 3: return "Reset while moving";
    case 4: return "Probe fail";
    case 5: return "Probe fail";
    case 6: return "Homing fail: reset";
    case 7: return "Homing fail: door";
    case 8: return "Homing fail: pull off";
    case 9: return "Homing fail: no switch";
    default: return "Unlock or home";
  }
}

const char * grbl_error_text(int code)
{
  switch (code) {
    case 1: return "Expected command letter";
    case 2: return "Bad number format";
    case 3: return "Invalid $ statement";
    case 8: return "Not idle";
    case 9: return "Locked: alarm or jog";
    case 15: return "Travel exceeded";
    case 20: return "Unsupported command";
    case 22: return "Feed rate not set";
    case 33: return "Invalid target";
    case 60: return "SD card not mounted";
    case 61: return "SD read failed";
    case 62: return "SD folder failed";
    case 64: return "SD file empty";
    case 65: return "SD file not found";
    case 66: return "SD open failed";
    case 67: return "SD card busy";
    case 120: return "Another client busy";
    case 130: return "Jog cancelled";
    default: return "Command rejected";
  }
}
