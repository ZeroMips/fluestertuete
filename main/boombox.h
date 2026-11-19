// The boombox: a Bluetooth speaker next to the CD ripper's Pi, which plays albums from the NAS on it (cd-bt).
// The player sends album, track and position there and reads back where it is. Calls block (HTTP on the LAN):
// make them from the player's task, never from the UI.
#pragma once
#include <stdbool.h>

#include "nas.h"

typedef enum { BB_STOPPED, BB_CONNECTING, BB_PLAYING, BB_PAUSED, BB_ERROR, BB_UNREACHABLE } boombox_state_t;

typedef struct {
    boombox_state_t state;
    char message[128];     // why it is not playing, in German, or ""
    char album[NAS_PATH_MAX];
    char track[256];       // file name
    float pos, dur;        // seconds
    bool speaker_connected;
    bool finished;  // the album played to its end
} boombox_status_t;

bool boombox_available(void);              // a boombox address is set up
bool boombox_status(boombox_status_t *s);  // false: the Pi does not answer (state BB_UNREACHABLE)
// Each returns the status after the command; false with s->message set when it failed.
bool boombox_play(const char *album, const char *track, int pos_s, boombox_status_t *s);
bool boombox_toggle(boombox_status_t *s);
bool boombox_stop(boombox_status_t *s);
bool boombox_seek(int delta_s, boombox_status_t *s);
bool boombox_step(int direction, boombox_status_t *s);
bool boombox_volume(int percent, boombox_status_t *s);
