// Incremental MP3 decoding: bytes go in as they arrive, whole frames of PCM come out.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MP3S_IN_SIZE 8192
#define MP3S_MAX_SAMPLES 2304  // one MPEG-1 frame, stereo

typedef enum { MP3S_FRAME, MP3S_NEED_DATA, MP3S_END } mp3s_result_t;

typedef struct {
    int sample_rate;
    int channels;
    int samples;  // total over all channels
    size_t bytes;  // input consumed for this frame, skipped junk included
} mp3s_frame_t;

typedef struct {
    void *dec;
    uint8_t in[MP3S_IN_SIZE];
    size_t rd, len;
    uint64_t consumed;  // bytes taken from the stream since the last reset
    uint64_t frame_mark;
    uint64_t skip;
    bool at_file_start;
    bool check_header;
    bool synced;  // the last frame decoded; a new position must prove itself first
    uint32_t file_frames;  // total audio frames from a Xing/Info header, 0 if unknown
    int16_t pcm[MP3S_MAX_SAMPLES];
} mp3s_t;

bool mp3s_init(mp3s_t *s);
void mp3s_deinit(mp3s_t *s);
// Call before feeding from a new position. Only offset 0 can carry an ID3v2 tag.
bool mp3s_reset(mp3s_t *s, bool at_file_start);
// Where to put new input and how much fits; follow with mp3s_commit().
size_t mp3s_space(mp3s_t *s, uint8_t **dst);
void mp3s_commit(mp3s_t *s, size_t n);
// eof says no more data will be fed. On MP3S_FRAME the samples are in s->pcm.
mp3s_result_t mp3s_next(mp3s_t *s, bool eof, mp3s_frame_t *f);
