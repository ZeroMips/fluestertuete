#include "util.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

// Next character for comparison: lower-case ASCII, with German letters folded to the base letter.
static int fold_next(const unsigned char **p)
{
    const unsigned char *s = *p;
    if (s[0] == 0xCC && s[1] == 0x88) {  // combining diaeresis of an NFD name: ignore it
        s += 2;
        *p = s;
        if (!*s) return 0;
    }
    if (s[0] == 0xC3 && s[1]) {
        *p = s + 2;
        switch (s[1]) {
        case 0x84: case 0xA4: return 'a';
        case 0x96: case 0xB6: return 'o';
        case 0x9C: case 0xBC: return 'u';
        case 0x9F: return 's';
        default: return 0x100 + s[1];
        }
    }
    *p = s + 1;
    return tolower(s[0]);
}

int nat_cmp(const char *a, const char *b)
{
    const unsigned char *x = (const unsigned char *)a, *y = (const unsigned char *)b;
    while (*x && *y) {
        if (isdigit(*x) && isdigit(*y)) {
            while (*x == '0') x++;
            while (*y == '0') y++;
            const unsigned char *xs = x, *ys = y;
            while (isdigit(*x)) x++;
            while (isdigit(*y)) y++;
            long lx = x - xs, ly = y - ys;
            if (lx != ly) return lx < ly ? -1 : 1;
            int c = memcmp(xs, ys, lx);
            if (c) return c;
            continue;
        }
        int cx = fold_next(&x), cy = fold_next(&y);
        if (cx != cy) return cx < cy ? -1 : 1;
    }
    if (*x) return 1;
    if (*y) return -1;
    return strcmp(a, b);
}

static size_t encode(const char *in, char *out, size_t out_size, bool keep_slash)
{
    static const char hex[] = "0123456789ABCDEF";
    size_t n = 0;
    for (const unsigned char *p = (const unsigned char *)in; *p; p++) {
        if (isalnum(*p) || *p == '-' || *p == '_' || *p == '.' || *p == '~' || (keep_slash && *p == '/')) {
            if (n + 2 > out_size) return 0;
            out[n++] = *p;
        } else {
            if (n + 4 > out_size) return 0;
            out[n++] = '%';
            out[n++] = hex[*p >> 4];
            out[n++] = hex[*p & 15];
        }
    }
    if (n >= out_size) return 0;
    out[n] = 0;
    return n;
}

size_t url_encode(const char *in, char *out, size_t out_size) { return encode(in, out, out_size, false); }
size_t url_encode_path(const char *in, char *out, size_t out_size) { return encode(in, out, out_size, true); }

// Some files were named on old systems: their bytes survive as U+FF80..U+FFFF, Latin-1 above 0xC0 and
// MacRoman below. Returns the Latin-1 code point, or 0 when the byte means nothing here.
static unsigned repair_byte(unsigned b)
{
    if (b >= 0xC0) return b;
    switch (b) {
    case 0x80: return 0xC4;  // Ä
    case 0x85: return 0xD6;  // Ö
    case 0x86: return 0xDC;  // Ü
    case 0x8A: return 0xE4;  // ä
    case 0x9A: return 0xF6;  // ö
    case 0x9F: return 0xFC;  // ü
    case 0xA7: return 0xDF;  // ß
    default: return 0;
    }
}

void display_name(const char *in, char *out, size_t out_size, bool strip_ext)
{
    static const char base[] = "aouAOU";
    static const unsigned char composed[] = {0xA4, 0xB6, 0xBC, 0x84, 0x96, 0x9C};
    size_t len = strlen(in);
    if (strip_ext) {
        const char *dot = strrchr(in, '.');
        if (dot && dot != in && strlen(dot) <= 5) len = dot - in;
    }
    size_t n = 0, i = 0;
    for (; i < len && n + 3 < out_size; i++) {
        const char *b = in[i] ? strchr(base, in[i]) : NULL;
        if (b && i + 2 < len && (unsigned char)in[i + 1] == 0xCC && (unsigned char)in[i + 2] == 0x88) {
            out[n++] = (char)0xC3;
            out[n++] = (char)composed[b - base];
            i += 2;
        } else if ((unsigned char)in[i] == 0xEF && i + 2 < len && ((unsigned char)in[i + 1] & 0xFE) == 0xBE) {
            unsigned c = repair_byte(((unsigned char)in[i + 1] & 1) << 6 | ((unsigned char)in[i + 2] & 0x3F) | 0x80);
            if (c) {
                out[n++] = (char)(0xC0 | c >> 6);
                out[n++] = (char)(0x80 | (c & 0x3F));
            } else {
                out[n++] = '?';
            }
            i += 2;
        } else {
            out[n++] = in[i];
        }
    }
    if (i < len) {  // cut short: do not leave half a UTF-8 sequence behind
        while (n && ((unsigned char)out[n - 1] & 0xC0) == 0x80) n--;
        if (n && ((unsigned char)out[n - 1] & 0xC0) == 0xC0) n--;
    }
    out[n] = 0;
}

bool json_string_array(const char *in, char *out, size_t out_size)
{
    size_t n = 0;
    if (out_size < 5) return false;
    out[n++] = '[';
    out[n++] = '"';
    for (const unsigned char *p = (const unsigned char *)in; *p; p++) {
        if (n + 8 > out_size) return false;
        if (*p == '"' || *p == '\\') {
            out[n++] = '\\';
            out[n++] = *p;
        } else if (*p < 0x20) {
            n += snprintf(out + n, out_size - n, "\\u%04x", *p);
        } else {
            out[n++] = *p;
        }
    }
    out[n++] = '"';
    out[n++] = ']';
    out[n] = 0;
    return true;
}

bool is_audio_name(const char *name)
{
    const char *dot = strrchr(name, '.');
    return dot && strcasecmp(dot, ".mp3") == 0 && !is_hidden_name(name);
}

bool is_hidden_name(const char *name)
{
    return name[0] == '.' || name[0] == '@' || name[0] == '#';
}

uint32_t fnv1a(const char *s)
{
    uint32_t h = 2166136261u;
    for (; *s; s++) h = (h ^ (unsigned char)*s) * 16777619u;
    return h;
}
