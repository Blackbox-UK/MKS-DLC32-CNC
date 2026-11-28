#ifndef GRBL_H
#define GRBL_H
#include <stdbool.h>
#include <stdint.h>

typedef enum {
  ST_UNKNOWN, ST_IDLE, ST_RUN, ST_HOLD, ST_JOG, ST_ALARM, ST_DOOR, ST_CHECK, ST_HOME, ST_SLEEP
} grbl_state_t;

#define MAX_FILES 40
#define NAME_LEN  48

typedef struct {
  bool connected;
  grbl_state_t state;
  char state_text[16];  // as reported, e.g. "Hold:0"
  float mpos[3], wpos[3], wco[3];
  float feed, spindle;
  int ov_feed, ov_rapid, ov_spindle;
  bool sd_running;
  float sd_pct;
  char sd_file[NAME_LEN];
  char pins[12];
  bool spindle_on, spindle_ccw, flood, mist;  // from the A: field
  long sd_line;            // current SD line number (DLC32 serial-screen build), 0 = unknown
  char sd_line_text[40];
  float spindle_max;       // $30, 0 until read
  char ip[20];             // from [ESP111]
  char version[32];

  // last message for the UI, seq changes on every new one
  uint32_t msg_seq;
  char msg[64];
  bool msg_is_error;

  // $SD/List result
  bool files_loading;
  uint32_t files_seq;
  int file_count;
  char file_name[MAX_FILES][NAME_LEN];
  uint32_t file_size[MAX_FILES];
} grbl_t;

extern grbl_t grbl;

void grbl_init(void);
void grbl_poll(void);              // call often from the main loop
bool grbl_send(const char * line); // queue a command line (no newline), false if queue full
void grbl_realtime(uint8_t c);     // send a real-time command byte now
bool grbl_idle_queue(void);        // nothing waiting for "ok"

// Grbl real-time commands
#define RT_STATUS      '?'
#define RT_HOLD        '!'
#define RT_RESUME      '~'
#define RT_RESET       0x18
#define RT_JOG_CANCEL  0x85
#define RT_FEED_100    0x90
#define RT_FEED_PLUS   0x91
#define RT_FEED_MINUS  0x92
#define RT_SPIN_100    0x99
#define RT_SPIN_PLUS   0x9A
#define RT_SPIN_MINUS  0x9B

const char * grbl_alarm_text(int code);
const char * grbl_error_text(int code);

// Console log of lines sent and received (status reports and "ok" left out)
#define LOG_LINES 40
#define LOG_LEN   56
enum { LOG_TX, LOG_RX, LOG_ERR };
typedef struct { uint8_t kind; char text[LOG_LEN]; } log_line_t;
extern log_line_t grbl_log[LOG_LINES];
extern int grbl_log_head;      // next slot to write
extern uint32_t grbl_log_seq;  // changes on every new line
void grbl_log_clear(void);

// for the PC simulator and tests: feed one received line
void grbl_handle_line(char * line);

#endif
