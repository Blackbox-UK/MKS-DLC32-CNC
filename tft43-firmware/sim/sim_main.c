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

int main(int argc, char ** argv)
{
  const char * out = argc > 1 ? argv[1] : ".";
  ui_init();
  run(100);
  shot(out, "0-no-link");

  sim_rx("\r\nGrbl 1.3a ['$' for help]\r\n");
  status("<Idle|MPos:12.500,-3.250,-1.000|FS:0,0|WCO:2.500,-3.250,-6.000>");
  expect(grbl.connected, "connected after first reply");
  expect(grbl.state == ST_IDLE, "state Idle");
  expect(grbl.wpos[0] > 9.99f && grbl.wpos[0] < 10.01f, "WPos X = MPos - WCO = 10.000");
  expect(grbl.wpos[2] > 4.99f && grbl.wpos[2] < 5.01f, "WPos Z = 5.000");
  run(500);
  shot(out, "1-main-idle");

  // zero X (button at 396,42)
  sim_txn = 0;
  tap(430, 60);
  expect_sent("G10 L20 P0 X0\n");
  ack();

  // jog screen, step 10 mm, X+
  tap(50, 248);  // Jog
  tap(330 + 2 * 37 + 10, 130);  // step 10
  sim_txn = 0;
  tap(6 + 2 * 76 + 30, 40 + 62 + 30);  // X+
  expect_sent("$J=G91 G21 X10.000 F1000\n");
  ack();
  status("<Jog|MPos:20.000,-3.250,-1.000|FS:1000,0>");
  shot(out, "2-jog");
  status("<Idle|MPos:22.500,-3.250,-1.000|FS:0,0>");
  tap(50, 248);  // Back

  // files
  sim_txn = 0;
  tap(4 + 3 * 96 + 40, 248);  // Files
  run(50);
  expect_sent("$SD/List\n");
  sim_rx("[FILE:/sign-face.nc|SIZE:183422]\r\n[FILE:/pcb-isolation-top.gcode|SIZE:2411093]\r\n"
         "[FILE:/test-square.nc|SIZE:812]\r\n[FILE:/logo_engrave_v2.nc|SIZE:95111]\r\nok\r\n");
  run(100);
  expect(grbl.file_count == 4, "4 files listed");
  tap(100, 38 + 36 + 10);  // second row
  shot(out, "3-files");
  tap(400, 248);  // Run
  shot(out, "4-confirm");
  sim_txn = 0;
  tap(300, 190);  // confirm Run
  expect_sent("$SD/Run=/pcb-isolation-top.gcode\n");
  ack();

  // running job
  status("<Run|MPos:42.120,17.880,-1.500|FS:600,12000|WCO:2.500,-3.250,-6.000|Ov:110,100,100|SD:37.45,/pcb-isolation-top.gcode>");
  run(300);
  expect(grbl.sd_running && grbl.sd_pct > 37.4f, "SD progress parsed");
  shot(out, "5-job-running");
  tap(50, 248);  // Pause
  expect(strchr(sim_tx, '!') != NULL, "Pause sends feed hold");
  status("<Hold:0|MPos:42.120,17.880,-1.500|FS:0,12000|Ov:110,100,100|SD:37.50,/pcb-isolation-top.gcode>");
  run(300);
  shot(out, "6-job-hold");

  // stop: feed hold first, controller reset once motion has stopped
  status("<Run|MPos:42.120,17.880,-1.500|FS:600,12000|SD:38.00,/pcb-isolation-top.gcode>");
  sim_txn = 0; sim_tx[0] = 0;
  tap(4 + 96 + 40, 248);  // Stop
  expect(strchr(sim_tx, '!') != NULL && strchr(sim_tx, 0x18) == NULL, "Stop sends feed hold, not reset yet");
  status("<Hold:1|MPos:42.300,17.880,-1.500|FS:200,12000|SD:38.00,/pcb-isolation-top.gcode>");
  expect(strchr(sim_tx, 0x18) == NULL, "no reset while still decelerating (Hold:1)");
  status("<Hold:0|MPos:42.400,17.880,-1.500|FS:0,12000|SD:38.00,/pcb-isolation-top.gcode>");
  expect(strchr(sim_tx, 0x18) != NULL, "reset sent once at rest (Hold:0)");

  // alarm
  sim_rx("ALARM:1\r\n");
  status("<Alarm|MPos:0.000,0.000,0.000|FS:0,0|Pn:X>");
  run(200);
  shot(out, "7-alarm");
  sim_txn = 0;
  tap(4 + 2 * 96 + 40, 248);  // Unlock
  expect_sent("$X\n");
  ack();

  status("<Idle|MPos:0.000,0.000,0.000|FS:0,0>");
  tap(4 + 4 * 96 + 40, 248);  // More
  shot(out, "8-more");

  // link loss
  run(3000);
  expect(!grbl.connected, "disconnect detected after 2 s of silence");

  printf(failures ? "\n%d check(s) FAILED\n" : "\nall checks passed\n", failures);
  return failures ? 1 : 0;
}
