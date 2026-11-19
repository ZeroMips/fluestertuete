#pragma once
#include "lvgl.h"

void ui_init(lv_display_t *disp);
void ui_boot_message(const char *text);
void ui_start(void);  // call once network and player are up
