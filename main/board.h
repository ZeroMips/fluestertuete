// The hardware: display, touch, buttons or keyboard, backlight, battery, speaker pins.
// board_tdeck.c: LilyGo T-Deck (ESP32-S3). board_core2.c: M5Stack Core2 v1.1 (ESP32, AXP2101).
#pragma once
#include <stdbool.h>
#include <stdint.h>

#include "lvgl.h"
#include "sdkconfig.h"

#if CONFIG_IDF_TARGET_ESP32
#define BOARD_NAME "core2"
#define BOARD_I2S_BCLK 12
#define BOARD_I2S_WS 0
#define BOARD_I2S_DOUT 2
#define BOARD_HAS_KEYBOARD 0
#else
#define BOARD_NAME "tdeck"
#define BOARD_I2S_BCLK 7
#define BOARD_I2S_WS 5
#define BOARD_I2S_DOUT 6
#define BOARD_HAS_KEYBOARD 1
#endif

typedef enum { IN_NONE, IN_UP, IN_DOWN, IN_LEFT, IN_RIGHT, IN_CLICK, IN_BACK, IN_ENTER, IN_CHAR } board_input_t;

typedef struct {
    board_input_t type;
    char ch;  // IN_CHAR: printable ASCII from the keyboard, space included
} board_event_t;

lv_display_t *board_init(void);
bool board_touch_read(int *x, int *y);
bool board_input_next(board_event_t *ev);  // call periodically until it returns false
void board_backlight(bool on);
int board_battery_percent(void);  // -1 when no battery is attached
