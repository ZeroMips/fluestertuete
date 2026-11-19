#pragma once
#include <stdbool.h>
#include <stdint.h>

void net_start(void);
bool net_wait(uint32_t timeout_ms);
