// Drives the real UI with scripted Grbl replies and touches, checks the
// commands sent to the controller, and writes a PPM screenshot per screen.
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "board.h"
#include "grbl.h"
#include "ui.h"

extern uint32_t sim_ms;
extern char sim_tx[];
extern int sim_txn;
void sim_rx(const char * s);
void sim_save_ppm(const char * path);

static int failures;

static void run(int ms)  // advance time, keep the controller "alive"
{
  for (int t = 0; t < ms; t += 10) {
    sim_ms += 10;
    grbl_poll();
    ui_step(false, 0, 0);
  }
}

static void status(const char * s)
{
  sim_rx(s);
  sim_rx("\r\n");
  run(50);
}

static void tap(int x, int y)
{
  ui_step(true, x, y);
  sim_ms += 80;
  ui_step(true, x, y);
  ui_step(false, 0, 0);
  run(30);
}

// Expect `cmd` among the lines sent since the last check; '?' polls are ignored.
static void expect_sent(const char * cmd)
{
  if (!strstr(sim_tx, cmd)) {
    printf("FAIL: expected \"%s\" to be sent, got:", cmd);
    for (int i = 0; i < sim_txn; i++) if (sim_tx[i] != '?') putchar(sim_tx[i] == '\n' ? '|' : sim_tx[i]);
    printf("\n");
    failures++;
  } else {
    printf("ok:   sent \"%s\"\n", cmd);
  }
  sim_txn = 0; sim_tx[0] = 0;
}

static void ack(void) { sim_rx("ok\r\n"); run(30); }

static void expect(int cond, const char * what)
{
  printf("%s %s\n", cond ? "ok:  " : "FAIL:", what);
  if (!cond) failures++;
}

static void shot(const char * dir, const char * name)
{
  char p[256];
  snprintf(p, sizeof(p), "%s/%s.ppm", dir, name);
  sim_save_ppm(p);
}

#define NAV(i) tap((i) * 80 + 40, 250)
#define TOPY 34

int main(int argc, char ** argv)
{
  const char * out = argc > 1 ? argv[1] : ".";
  ui_init();
  run(100);
  shot(out, "00-no-link");

  sim_rx("\r\nGrbl 1.3a ['$' for help]\r\n");
  status("<Idle|MPos:12.500,-3.250,-1.000|FS:0,0|WCO:2.500,-3.250,-6.000>");
  expect(grbl.connected, "connected after first reply");
  expect(grbl.state == ST_IDLE, "state Idle");
  expect(grbl.wpos[0] > 9.99f && grbl.wpos[0] < 10.01f, "WPos X = MPos - WCO = 10.000");
  expect(grbl.wpos[2] > 4.99f && grbl.wpos[2] < 5.01f, "WPos Z = 5.000");
  run(500);
  shot(out, "01-dro-idle");

  sim_txn = 0;
  tap(436, TOPY + 20);  // X zero
  expect_sent("G10 L20 P0 X0\n");
  ack();

  // Move
  NAV(1);
  tap(276 + 2 * 50 + 20, 110);  // step 10
  sim_txn = 0;
  tap(6 + 2 * 64 + 30, TOPY + 64 + 30);  // X+
  expect_sent("$J=G91 G21 X10.000 F1000\n");
  ack();
  status("<Jog|MPos:20.000,-3.250,-1.000|FS:1000,0>");
  shot(out, "02-move");
  status("<Idle|MPos:22.500,-3.250,-1.000|FS:0,0>");
  sim_txn = 0;
  tap(426 + 25, 178 + 22);  // Probe -> confirm
  shot(out, "03-confirm-probe");
  tap(310, 188);  // OK
  expect_sent("G38.2 G91 Z-50 F80\n");
  ack(); ack(); ack(); ack();

  // Tool
  sim_txn = 0;
  NAV(3);
  expect_sent("$$\n");
  sim_rx("$30=12000\r\n$31=0\r\nok\r\n");
  run(50);
  expect(grbl.spindle_max > 11999, "$30 read as spindle max");
  sim_txn = 0;
  tap(106 + 92 + 40, TOPY + 52);  // 50%
  expect_sent("M3 S6000\n");
  ack();
  status("<Idle|MPos:22.500,-3.250,-1.000|FS:0,6000|A:SF>");
  expect(grbl.spindle_on && grbl.flood, "A:SF parsed as spindle on + flood");
  run(100);
  shot(out, "04-tool");
  sim_txn = 0;
  tap(14 + 42, TOPY + 30 + 22);  // Off
  expect_sent("M5\n");
  ack();
  status("<Idle|MPos:22.500,-3.250,-1.000|FS:0,0>");

  // Files
  sim_txn = 0;
  NAV(2);
  run(50);
  expect_sent("$SD/List\n");
  sim_rx("[FILE:/sign-face.nc|SIZE:183422]\r\n[FILE:/pcb-isolation-top.gcode|SIZE:2411093]\r\n"
         "[FILE:/test-square.nc|SIZE:812]\r\n[FILE:/logo_engrave_v2.nc|SIZE:95111]\r\nok\r\n");
  run(100);
  expect(grbl.file_count == 4, "4 files listed");
  tap(100, TOPY + 38 + 15);  // second row
  shot(out, "05-files");
  tap(436, TOPY + 144 + 20);  // Run
  shot(out, "06-confirm-run");
  sim_txn = 0;
  tap(310, 188);  // OK
  expect_sent("$SD/Run=/pcb-isolation-top.gcode\n");
  ack();

  // running job (DRO shows job controls)
  status("<Run|MPos:42.120,17.880,-1.500|FS:600,12000|WCO:2.500,-3.250,-6.000|Ov:110,100,100|A:S"
         "|SD:37.45,/pcb-isolation-top.gcode|SDLn:1532:G1 X40.12 Y21.13 F600>");
  run(300);
  expect(grbl.sd_running && grbl.sd_pct > 37.4f, "SD progress parsed");
  expect(grbl.sd_line == 1532 && !strcmp(grbl.sd_line_text, "G1 X40.12 Y21.13 F600"), "current SD line parsed");
  shot(out, "07-dro-job");
  sim_txn = 0; sim_tx[0] = 0;
  tap(436, TOPY + 20);  // Pause
  expect(strchr(sim_tx, '!') != NULL, "Pause sends feed hold");
  status("<Hold:0|MPos:42.120,17.880,-1.500|FS:0,12000|Ov:110,100,100|SD:37.50,/pcb-isolation-top.gcode|SDLn:1533:G1 X42.4>");
  run(300);
  shot(out, "08-dro-hold");

  // console
  NAV(4);
  run(100);
  shot(out, "09-code");

  // stop: confirm, feed hold, reset once at rest
  NAV(0);
  status("<Run|MPos:42.120,17.880,-1.500|FS:600,12000|SD:38.00,/pcb-isolation-top.gcode>");
  sim_txn = 0; sim_tx[0] = 0;
  tap(436, TOPY + 50 + 20);  // Stop
  tap(310, 188);             // OK
  expect(strchr(sim_tx, '!') != NULL && strchr(sim_tx, 0x18) == NULL, "Stop sends feed hold, not reset yet");
  status("<Hold:1|MPos:42.300,17.880,-1.500|FS:200,12000|SD:38.00,/pcb-isolation-top.gcode>");
  expect(strchr(sim_tx, 0x18) == NULL, "no reset while still decelerating (Hold:1)");
  status("<Hold:0|MPos:42.400,17.880,-1.500|FS:0,12000|SD:38.00,/pcb-isolation-top.gcode>");
  expect(strchr(sim_tx, 0x18) != NULL, "reset sent once at rest (Hold:0)");

  // alarm
  sim_rx("ALARM:1\r\n");
  status("<Alarm|MPos:0.000,0.000,0.000|FS:0,0|Pn:X>");
  run(200);
  shot(out, "10-dro-alarm");
  sim_txn = 0;
  tap(312, 205);  // Unlock
  expect_sent("$X\n");
  ack();
  status("<Idle|MPos:0.000,0.000,0.000|FS:0,0>");

  // console keyboard: type G0X5 and send
  NAV(4);
  tap(395 + 19, 186 + 19);  // keyboard
  tap(4 + 4 * 47 + 20, 46 + 2 * 44 + 20);  // G
  tap(4 + 9 * 47 + 20, 46 + 20);           // 0
  tap(4 + 1 * 47 + 20, 46 + 3 * 44 + 20);  // X
  tap(4 + 4 * 47 + 20, 46 + 20);           // 5
  shot(out, "11-keyboard");
  sim_txn = 0;
  tap(408 + 34, 226 + 21);  // Send
  expect_sent("G0X5\n");
  ack();

  // setup + wifi
  NAV(5);
  shot(out, "12-setup");
  sim_txn = 0;
  tap(244 + 116, TOPY + 22);  // WiFi
  expect_sent("[ESP111]\n");
  sim_rx("192.168.1.57\r\nok\r\n");
  run(100);
  expect(!strcmp(grbl.ip, "192.168.1.57"), "IP from [ESP111]");
  shot(out, "13-wifi");

  // link loss
  run(3000);
  expect(!grbl.connected, "disconnect detected after 2 s of silence");

  printf(failures ? "\n%d check(s) FAILED\n" : "\nall checks passed\n", failures);
  return failures ? 1 : 0;
}
