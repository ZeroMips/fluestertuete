#include "search.h"

#include <stdlib.h>
#include <string.h>

#include "util.h"

#define NORM_MAX 256
#define MAX_TOKENS 6
#define TOKEN_MAX 32
#define WORD_MAX 48

typedef struct {
    const char *path;
    char kind;
    uint32_t size;
    const char *name;    // normalized last path component
    const char *parent;  // normalized folders above it, below the root
} entry_t;

struct search_index {
    char *text;
    size_t len;
    entry_t *e;
    int n;
    char *norm;  // all normalized strings in one block: thousands of tiny allocations would fill internal RAM
};

// Appends the folded form of one UTF-8 character. Returns bytes consumed from s.
static int fold_char(const unsigned char *s, char *out, size_t *n, size_t size)
{
    const char *rep = NULL;
    int used = 1;
    char one[2] = {0};
    if (s[0] < 0x80) {
        unsigned char c = s[0];
        if (c >= 'A' && c <= 'Z') c += 32;
        one[0] = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ? (char)c : ' ';
        rep = one;
    } else if (s[0] == 0xC3 && s[1]) {
        used = 2;
        unsigned c = s[1] | 0x20;  // lower case of Latin-1 letters
        if (s[1] == 0x9F) rep = "ss";
        else if (c >= 0xA0 && c <= 0xA6) rep = "a";
        else if (c == 0xA7) rep = "c";
        else if (c >= 0xA8 && c <= 0xAB) rep = "e";
        else if (c >= 0xAC && c <= 0xAF) rep = "i";
        else if (c == 0xB1) rep = "n";
        else if ((c >= 0xB2 && c <= 0xB6) || c == 0xB8) rep = "o";
        else if (c >= 0xB9 && c <= 0xBC) rep = "u";
        else if (c == 0xBD || c == 0xBF) rep = "y";
        else rep = " ";
    } else {
        // Other scripts and symbols: skip the whole sequence.
        used = s[0] >= 0xF0 ? 4 : s[0] >= 0xE0 ? 3 : 2;
        for (int i = 1; i < used; i++)
            if (!s[i]) return i;
        rep = " ";
    }
    for (; *rep && *n + 1 < size; rep++) {
        if (*rep == ' ' && (*n == 0 || out[*n - 1] == ' ')) continue;
        // "ue" for u-umlaut is a common way to type it: fold it on both sides the same way.
        if ((*rep == 'e') && *n && (out[*n - 1] == 'a' || out[*n - 1] == 'o' || out[*n - 1] == 'u')) continue;
        out[(*n)++] = *rep;
    }
    return used;
}

void search_normalize(const char *in, char *out, size_t out_size)
{
    char tmp[NORM_MAX];
    display_name(in, tmp, sizeof(tmp), false);  // compose NFD umlauts, repair mangled names
    size_t n = 0;
    for (const unsigned char *s = (const unsigned char *)tmp; *s;) s += fold_char(s, out, &n, out_size);
    while (n && out[n - 1] == ' ') n--;
    out[n] = 0;
}

// Normalizes into out (NORM_MAX bytes) and returns the length.
static size_t norm_into(const char *s, size_t len, bool strip_ext, char *out)
{
    char raw[NORM_MAX];
    if (len >= sizeof(raw)) len = sizeof(raw) - 1;
    memcpy(raw, s, len);
    raw[len] = 0;
    if (strip_ext) {
        char *dot = strrchr(raw, '.');
        if (dot && strlen(dot) <= 5) *dot = 0;
    }
    for (char *p = raw; *p; p++)
        if (*p == '/') *p = ' ';
    search_normalize(raw, out, NORM_MAX);
    return strlen(out);
}

search_index_t *search_index_parse(char *text, size_t len)
{
    search_index_t *idx = calloc(1, sizeof(*idx));
    if (!idx) return NULL;
    idx->text = text;
    idx->len = len;
    int lines = 0;
    for (size_t i = 0; i < len; i++) lines += text[i] == '\n';
    // Normalized text is never longer than the original, plus a terminator per string.
    size_t cap = len + 2 * (size_t)lines + 2, used = 0;
    idx->e = calloc(lines + 1, sizeof(entry_t));
    idx->norm = malloc(cap);
    if (!idx->e || !idx->norm) {
        free(idx->e);
        free(idx->norm);
        free(idx);
        return NULL;
    }
    size_t *offs = calloc(2 * (size_t)lines + 2, sizeof(size_t));
    if (!offs) {
        free(idx->e);
        free(idx->norm);
        free(idx);
        return NULL;
    }
    char buf[NORM_MAX];
    for (char *line = text, *end; line < text + len && (end = memchr(line, '\n', text + len - line)); line = end + 1) {
        *end = 0;
        if (end - line < 3 || line[1] != '/') continue;
        entry_t *e = &idx->e[idx->n];
        e->kind = line[0];
        e->path = line + 1;
        char *tab = memchr(line, '\t', end - line);
        if (tab) {
            *tab = 0;
            e->size = (uint32_t)strtoul(tab + 1, NULL, 10);
        }
        const char *base = strrchr(e->path, '/');
        const char *after_root = strchr(e->path + 1, '/');  // paths below the root: its name says nothing
        if (!after_root) continue;
        size_t parent_len = base > after_root ? (size_t)(base - after_root - 1) : 0;
        size_t n1 = norm_into(base + 1, strlen(base + 1), e->kind == SEARCH_TRACK, buf);
        if (used + n1 + 1 > cap) break;
        memcpy(idx->norm + used, buf, n1 + 1);
        offs[2 * idx->n] = used;
        used += n1 + 1;
        size_t n2 = norm_into(after_root + 1, parent_len, false, buf);
        if (used + n2 + 1 > cap) break;
        memcpy(idx->norm + used, buf, n2 + 1);
        offs[2 * idx->n + 1] = used;
        used += n2 + 1;
        idx->n++;
    }
    for (int i = 0; i < idx->n; i++) {
        idx->e[i].name = idx->norm + offs[2 * i];
        idx->e[i].parent = idx->norm + offs[2 * i + 1];
    }
    free(offs);
    return idx;
}

void search_index_free(search_index_t *idx)
{
    if (!idx) return;
    free(idx->e);
    free(idx->norm);
    free(idx->text);
    free(idx);
}

int search_index_children(const search_index_t *idx, const char *dir, search_child_t *out, int max)
{
    size_t dl = strlen(dir);
    int n = 0;
    bool known = false;
    for (int i = 0; i < idx->n; i++) {
        const entry_t *e = &idx->e[i];
        if (strncmp(e->path, dir, dl) != 0) continue;
        if (e->path[dl] == 0) {
            known = true;
            continue;
        }
        if (e->path[dl] != '/' || strchr(e->path + dl + 1, '/')) continue;
        known = true;
        if (n < max) out[n] = (search_child_t){.name = e->path + dl + 1, .kind = e->kind, .size = e->size};
        n++;
    }
    return known ? (n < max ? n : max) : -1;
}

int search_index_count(const search_index_t *idx) { return idx ? idx->n : 0; }

const char *search_index_text(const search_index_t *idx, size_t *len)
{
    *len = idx->len;
    return idx->text;
}

// Edit distance between t and the closest prefix of w, with transpositions ("hnude" -> "hunde").
static int prefix_distance(const char *t, int tl, const char *w, int wl)
{
    int rows[3][WORD_MAX + 1] = {{0}};
    int *pp = rows[0], *p = rows[1], *c = rows[2];
    if (wl > WORD_MAX) wl = WORD_MAX;
    for (int j = 0; j <= wl; j++) p[j] = j;
    for (int i = 1; i <= tl; i++) {
        c[0] = i;
        for (int j = 1; j <= wl; j++) {
            int cost = t[i - 1] != w[j - 1];
            int v = p[j - 1] + cost;
            if (p[j] + 1 < v) v = p[j] + 1;
            if (c[j - 1] + 1 < v) v = c[j - 1] + 1;
            if (i > 1 && j > 1 && t[i - 1] == w[j - 2] && t[i - 2] == w[j - 1] && pp[j - 2] + 1 < v) v = pp[j - 2] + 1;
            c[j] = v;
        }
        int *tmp = pp;
        pp = p;
        p = c;
        c = tmp;
    }
    int best = p[0];
    for (int j = 1; j <= wl; j++)
        if (p[j] < best) best = p[j];
    return best;
}

static int word_score(const char *t, int tl, const char *w, int wl)
{
    if (wl >= tl && memcmp(w, t, tl) == 0) return wl == tl ? 100 : 85;
    if (tl >= 3 && wl > tl) {
        for (int i = 1; i + tl <= wl; i++)
            if (memcmp(w + i, t, tl) == 0) return 60;
    }
    // Typos, but not in the first letter: that is rarely where they happen, and it keeps "kastner" off "master".
    int max_dist = tl >= 8 ? 2 : tl >= 4 ? 1 : 0;
    if (!max_dist || w[0] != t[0]) return 0;
    int d = prefix_distance(t, tl, w, wl);
    return d <= max_dist ? 50 - 15 * d : 0;
}

static int text_score(const char *t, int tl, const char *text)
{
    int best = 0;
    for (const char *w = text; *w;) {
        const char *end = strchr(w, ' ');
        int wl = end ? (int)(end - w) : (int)strlen(w);
        int s = word_score(t, tl, w, wl);
        if (s > best) best = s;
        if (!end) break;
        w = end + 1;
    }
    return best;
}

int search_query(const search_index_t *idx, const char *query, search_hit_t *hits, int max_hits)
{
    char norm[NORM_MAX];
    char tokens[MAX_TOKENS][TOKEN_MAX];
    int tlen[MAX_TOKENS], nt = 0;
    if (!idx || max_hits <= 0) return 0;
    search_normalize(query, norm, sizeof(norm));
    for (char *w = norm; *w && nt < MAX_TOKENS;) {
        char *end = strchr(w, ' ');
        int l = end ? (int)(end - w) : (int)strlen(w);
        if (l >= TOKEN_MAX) l = TOKEN_MAX - 1;
        memcpy(tokens[nt], w, l);
        tokens[nt][l] = 0;
        tlen[nt++] = l;
        if (!end) break;
        w = end + 1;
    }
    if (!nt) return 0;

    int found = 0;
    for (int i = 0; i < idx->n; i++) {
        const entry_t *e = &idx->e[i];
        int total = 0;
        bool own = false, all = true;
        for (int k = 0; k < nt && all; k++) {
            int s_name = text_score(tokens[k], tlen[k], e->name);
            int s_parent = text_score(tokens[k], tlen[k], e->parent) / 2;
            own |= s_name > 0;
            all = s_name || s_parent;
            total += s_name > s_parent ? s_name : s_parent;
        }
        // A track has to match by its own title: a series name alone would list every one of its tracks.
        // Folders may match by the series they are in, so typing "bibi" offers all of Bibi's albums.
        if (!all || (!own && e->kind == SEARCH_TRACK)) continue;
        total = total * 4 / nt;
        if (e->kind != SEARCH_TRACK) total += 40;
        int nl = (int)strlen(e->name);
        total -= (nl > 60 ? 60 : nl) / 4;  // closer to the query beats a long title with it somewhere inside

        int pos = found < max_hits ? found++ : max_hits;
        if (pos == max_hits && total <= hits[max_hits - 1].score) continue;
        if (pos == max_hits) pos = max_hits - 1;
        while (pos > 0 && hits[pos - 1].score < total) {
            hits[pos] = hits[pos - 1];
            pos--;
        }
        hits[pos] = (search_hit_t){.path = e->path, .kind = e->kind, .score = total};
    }
    return found;
}
