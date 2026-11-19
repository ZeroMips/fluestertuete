// Audio pipeline: NAS -> ring buffer -> MP3 decoder -> I2S amplifier. Or, with the boombox as output, the same
// commands go to the CD ripper's Pi, which plays the album on a Bluetooth speaker (boombox.h).
#pragma once
#include <stdbool.h>
#include <stdint.h>

#include "nas.h"

#define PLAYER_VOLUME_MAX 10

typedef enum { PLAYER_STOPPED, PLAYER_BUFFERING, PLAYER_PLAYING, PLAYER_PAUSED, PLAYER_ERROR } player_state_t;

typedef struct {
    player_state_t state;
    bool offline;  // the stream keeps retrying
    bool finished;
    int track;
    int track_count;
    int pos_s;
    int dur_s;
    char album[NAS_PATH_MAX];
    char track_name[256];
    bool boombox;       // output is the boombox, not the built-in speaker
    char message[128];  // boombox: why it is not playing, or ""
} player_status_t;

void player_init(void);
// Takes over tracks. Starts at the given track and byte offset.
void player_play(const char *album, nas_entry_t *tracks, int count, int track, uint32_t offset, uint32_t bytes_per_sec);
void player_toggle(void);
void player_seek(int seconds);
void player_step(int direction);  // next or previous track
void player_jump(int track);
void player_get_status(player_status_t *s);
// Caller frees with nas_free_entries().
bool player_copy_tracks(nas_entry_t **tracks, int *count);
void player_set_boombox(bool on);  // switches the output; what plays continues there
int player_volume(void);
void player_set_volume(int volume);
