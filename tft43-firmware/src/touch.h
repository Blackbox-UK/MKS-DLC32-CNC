#ifndef TOUCH_H
#define TOUCH_H
#include <stdbool.h>
#include <stdint.h>

void touch_init(void);
bool touch_has_calibration(void);  // false: run touch_calibrate() first
bool touch_pen_down(void);
bool touch_read(int * x, int * y);  // calibrated screen coordinates, false if not pressed
void touch_calibrate(void);         // interactive 3-point calibration, saved to flash

#endif
