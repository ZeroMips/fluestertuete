// Flash access switches the cache off, and on the ESP32 a task whose stack lives in PSRAM must not do that
// (it asserts). flash_run() calls fn(arg) right away on a task with an internal stack, else hands it to a
// small helper task with one and waits. Everything that reads or writes flash at run time goes through here.
#pragma once

void flash_guard_init(void);
void flash_run(void (*fn)(void *), void *arg);
