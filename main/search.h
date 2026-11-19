// Fuzzy search over the library index. Pure C, also compiled by the host tests.
#pragma once
#include <stddef.h>
#include <stdint.h>

// Index text: one entry per line, "<kind><path>", tracks followed by "\t<size in bytes>".
// Kinds: S folder, A album (holds tracks), T track.
#define SEARCH_FOLDER 'S'
#define SEARCH_ALBUM 'A'
#define SEARCH_TRACK 'T'

typedef struct search_index search_index_t;

typedef struct {
    const char *path;
    char kind;
    int score;
} search_hit_t;

// Takes ownership of text (malloc'ed, NUL-terminated). Returns NULL when out of memory.
search_index_t *search_index_parse(char *text, size_t len);
void search_index_free(search_index_t *idx);
int search_index_count(const search_index_t *idx);
const char *search_index_text(const search_index_t *idx, size_t *len);

typedef struct {
    const char *name;  // points into the index
    char kind;
    uint32_t size;
} search_child_t;

// Direct children of dir, in index order. Returns -1 when dir is not in the index.
int search_index_children(const search_index_t *idx, const char *dir, search_child_t *out, int max);

// Lower case, umlauts and accents folded, punctuation as single spaces: "Gr\xC3\xBCn, Bl\xC3\xA4tter!" -> "grun blatter".
void search_normalize(const char *in, char *out, size_t out_size);

// Best matches first. Tolerates typos and missing umlauts; every query word has to match.
int search_query(const search_index_t *idx, const char *query, search_hit_t *hits, int max_hits);
