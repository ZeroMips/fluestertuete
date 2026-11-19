// The NAS: directory listing and ranged file streaming, through a Synology share link (the device holds the
// link's password, never an account password) or WebDAV (any NAS, Nextcloud, ...). config() picks which.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "config.h"

// The folder with the audio dramas, as the NAS sees it (Synology share link: its folder; WebDAV: a path below
// the DAV root). Every path the player uses starts with it.
#define NAS_ROOT (config()->root)
#define NAS_PATH_MAX 512

typedef struct {
    char *name;
    uint32_t size;
    bool is_dir;
} nas_entry_t;

typedef enum { NAS_OK, NAS_ERR_NET, NAS_ERR_AUTH, NAS_ERR_API } nas_err_t;

typedef struct nas_stream nas_stream_t;

void nas_init(void);
bool nas_supported(void);  // false: this firmware was built without the configured NAS type
nas_err_t nas_login(void);
// Sorted: folders first, then files, both in natural order. Hidden entries are left out.
nas_err_t nas_list(const char *dir, nas_entry_t **entries, int *count);

// Keeps one connection open across listings: a crawl costs one TLS handshake, not one per folder.
typedef struct nas_lister nas_lister_t;
nas_lister_t *nas_lister_new(void);
nas_err_t nas_list_with(nas_lister_t *l, const char *dir, nas_entry_t **entries, int *count);
void nas_lister_free(nas_lister_t *l);
void nas_free_entries(nas_entry_t *entries, int count);
void nas_sort_entries(nas_entry_t *entries, int count);  // the order nas_list() returns

// Starts reading the file at offset. Logs in again when the session has expired.
nas_err_t nas_stream_open(const char *path, uint32_t offset, nas_stream_t **out);
int nas_stream_read(nas_stream_t *s, uint8_t *buf, size_t n);  // <= 0: ended or failed
void nas_stream_close(nas_stream_t *s);
