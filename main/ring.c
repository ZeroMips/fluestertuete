#include "ring.h"

#include <string.h>

void ring_init(ring_t *r, uint8_t *buf, size_t cap)
{
    r->buf = buf;
    r->cap = cap;
    ring_reset(r);
}

void ring_reset(ring_t *r)
{
    r->rd = 0;
    r->used = 0;
}

size_t ring_used(const ring_t *r) { return r->used; }
size_t ring_free(const ring_t *r) { return r->cap - r->used; }

size_t ring_write(ring_t *r, const uint8_t *p, size_t n)
{
    if (n > ring_free(r)) n = ring_free(r);
    size_t wr = (r->rd + r->used) % r->cap;
    size_t first = r->cap - wr < n ? r->cap - wr : n;
    memcpy(r->buf + wr, p, first);
    memcpy(r->buf, p + first, n - first);
    r->used += n;
    return n;
}

size_t ring_read(ring_t *r, uint8_t *p, size_t n)
{
    if (n > r->used) n = r->used;
    size_t first = r->cap - r->rd < n ? r->cap - r->rd : n;
    memcpy(p, r->buf + r->rd, first);
    memcpy(p + first, r->buf, n - first);
    r->rd = (r->rd + n) % r->cap;
    r->used -= n;
    return n;
}
