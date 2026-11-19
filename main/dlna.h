// Change signal from the Synology Media Server (DLNA/UPnP): its SystemUpdateID goes up whenever the media
// library changes. Plain HTTP on the LAN, no login; used only as a trigger, never for content.
#pragma once
#include <stdbool.h>
#include <stdint.h>

bool dlna_system_update_id(uint32_t *id);  // false when Media Server does not answer
