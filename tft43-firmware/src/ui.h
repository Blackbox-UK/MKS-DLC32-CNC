#ifndef UI_H
#define UI_H
#include <stdbool.h>

void ui_init(void);
// Run one UI step: touch input + redraw what changed. touch_down/x/y are the
// current (calibrated) touch state.
void ui_step(bool touch_down, int x, int y);

typedef enum { SCR_DRO, SCR_JOG, SCR_FILES, SCR_TOOL, SCR_CONSOLE, SCR_SETUP, SCR_WIFI } screen_t;
void ui_show(screen_t s);  // used by the simulator to take screenshots
extern bool ui_request_calibration;  // set by the Setup screen

#endif
