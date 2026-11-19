// Settings and listening positions, kept across power-off.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t track_hash;  // fnv1a of the file name, so renumbering a folder cannot resume the wrong file
    uint32_t offset;
    uint32_t bytes_per_sec;
} store_pos_t;

void store_init(void);
bool store_get_pos(const char *album, store_pos_t *pos);
void store_set_pos(const char *album, const store_pos_t *pos);
void store_clear_pos(const char *album);
int store_get_volume(int fallback);
void store_set_volume(int volume);
bool store_get_boombox(void);
void store_set_boombox(bool on);
bool store_get_last(char *path, size_t size);
void store_set_last(const char *path);
