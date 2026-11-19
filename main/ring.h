// Byte ring buffer. Not thread-safe: the player guards it with a mutex.
#pragma once
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint8_t *buf;
    size_t cap;
    size_t rd;
    size_t used;
} ring_t;

void ring_init(ring_t *r, uint8_t *buf, size_t cap);
void ring_reset(ring_t *r);
size_t ring_used(const ring_t *r);
size_t ring_free(const ring_t *r);
size_t ring_write(ring_t *r, const uint8_t *p, size_t n);  // returns bytes stored
size_t ring_read(ring_t *r, uint8_t *p, size_t n);         // returns bytes taken
