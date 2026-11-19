#include "mp3s.h"

#include <string.h>

#include "mp3dec.h"

// Holds any complete frame, so a decode attempt never sees a truncated one before EOF.
#define MIN_INPUT MAINBUF_SIZE

bool mp3s_init(mp3s_t *s)
{
    memset(s, 0, sizeof(*s));
    return mp3s_reset(s, true);
}

void mp3s_deinit(mp3s_t *s)
{
    if (s->dec) MP3FreeDecoder(s->dec);
    s->dec = NULL;
}

bool mp3s_reset(mp3s_t *s, bool at_file_start)
{
    // A fresh decoder: bit reservoir and overlap state from the old position would only make noise.
    mp3s_deinit(s);
    s->dec = MP3InitDecoder();
    s->rd = s->len = 0;
    s->consumed = s->skip = s->frame_mark = 0;
    s->at_file_start = s->check_header = at_file_start;
    s->file_frames = 0;
    s->synced = false;
    return s->dec != NULL;
}

size_t mp3s_space(mp3s_t *s, uint8_t **dst)
{
    if (s->rd) {
        memmove(s->in, s->in + s->rd, s->len);
        s->rd = 0;
    }
    *dst = s->in + s->len;
    return sizeof(s->in) - s->len;
}

void mp3s_commit(mp3s_t *s, size_t n) { s->len += n; }

static void drop(mp3s_t *s, size_t n)
{
    s->rd += n;
    s->len -= n;
    s->consumed += n;
}

// The first frame of most files is a Xing/Info header, the only exact source for the length of a VBR file.
static void parse_header(mp3s_t *s)
{
    const uint8_t *p = s->in + s->rd;
    size_t n = s->len < 64 ? s->len : 64;
    for (size_t i = 4; i + 12 <= n; i++) {
        if (memcmp(p + i, "Xing", 4) && memcmp(p + i, "Info", 4)) continue;
        if (p[i + 7] & 1) s->file_frames = (uint32_t)p[i + 8] << 24 | p[i + 9] << 16 | p[i + 10] << 8 | p[i + 11];
        return;
    }
}

// Audio data contains false sync words. A real frame header has another one right after its frame.
static bool frame_chain_ok(mp3s_t *s, bool eof)
{
    unsigned char *p = s->in + s->rd;
    MP3FrameInfo a, b;
    if (MP3GetNextFrameInfo(s->dec, &a, p) != ERR_MP3_NONE || a.layer != 3 || !a.bitrate || !a.samprate) return false;
    size_t len = (a.version == MPEG1 ? 144 : 72) * (size_t)a.bitrate / a.samprate + ((p[2] >> 1) & 1);
    if (len + 4 > s->len) return eof;
    return p[len] == 0xFF && (p[len + 1] & 0xE0) == 0xE0 && MP3GetNextFrameInfo(s->dec, &b, p + len) == ERR_MP3_NONE &&
           b.layer == 3 && b.samprate == a.samprate && b.version == a.version;
}

mp3s_result_t mp3s_next(mp3s_t *s, bool eof, mp3s_frame_t *f)
{
    for (;;) {
        if (s->skip) {
            size_t n = s->skip < s->len ? (size_t)s->skip : s->len;
            drop(s, n);
            s->skip -= n;
            if (s->skip) return eof ? MP3S_END : MP3S_NEED_DATA;
        }
        if (s->at_file_start) {
            if (s->len < 10 && !eof) return MP3S_NEED_DATA;
            s->at_file_start = false;
            const uint8_t *h = s->in + s->rd;
            if (s->len >= 10 && memcmp(h, "ID3", 3) == 0) {
                uint32_t size = ((uint32_t)(h[6] & 0x7F) << 21) | ((h[7] & 0x7F) << 14) | ((h[8] & 0x7F) << 7) | (h[9] & 0x7F);
                s->skip = 10 + (uint64_t)size + ((h[5] & 0x10) ? 10 : 0);
                continue;
            }
        }
        if (s->len < MIN_INPUT && !eof) return MP3S_NEED_DATA;
        if (s->len == 0) return MP3S_END;

        int off = MP3FindSyncWord(s->in + s->rd, (int)s->len);
        if (off < 0) {
            drop(s, s->len > 1 ? s->len - 1 : s->len);  // the last byte may be half a sync word
            if (eof) return MP3S_END;
            return MP3S_NEED_DATA;
        }
        drop(s, off);
        if (s->len < MIN_INPUT && !eof) return MP3S_NEED_DATA;
        if (s->len < 4) return MP3S_END;

        if (!s->synced && !frame_chain_ok(s, eof)) {
            drop(s, 1);
            continue;
        }
        if (s->check_header) {
            s->check_header = false;
            parse_header(s);
        }
        unsigned char *p = s->in + s->rd;
        int left = (int)s->len;
        int err = MP3Decode(s->dec, &p, &left, s->pcm, 0);
        size_t used = s->len - (size_t)left;
        drop(s, used);

        if (err == ERR_MP3_NONE) {
            MP3FrameInfo info;
            MP3GetLastFrameInfo(s->dec, &info);
            if (info.layer != 3 || !info.nChans || !info.outputSamps) {
                s->synced = false;
                continue;
            }
            s->synced = true;
            f->sample_rate = info.samprate;
            f->channels = info.nChans;
            f->samples = info.outputSamps;
            f->bytes = (size_t)(s->consumed - s->frame_mark);
            s->frame_mark = s->consumed;
            return MP3S_FRAME;
        }
        if (err != ERR_MP3_MAINDATA_UNDERFLOW) s->synced = false;
        if (err == ERR_MP3_INDATA_UNDERFLOW) {
            if (eof) return MP3S_END;
            if (s->len >= sizeof(s->in) / 2) drop(s, 1);  // plenty of input: this sync was false
            else return MP3S_NEED_DATA;
        } else if (err != ERR_MP3_MAINDATA_UNDERFLOW && used == 0) {
            drop(s, 1);  // false sync inside the data
        }
    }
}
