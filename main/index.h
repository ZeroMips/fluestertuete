// The library index for search and browsing: crawled from the NAS in the background, cached in flash.
#pragma once
#include <stdint.h>

#include "nas.h"
#include "search.h"

typedef enum { INDEX_NONE, INDEX_UNCHANGED, INDEX_UPDATED, INDEX_FAILED } index_result_t;

void index_init(void);  // loads the cached index
// Crawls the share 15 s after boot, when Media Server (DLNA) reports a change, and at least every 3 hours.
void index_start_refresh(void);
void index_request_refresh(void);  // crawl now, unless a crawl is running
uint32_t index_crawls(void);       // crawls finished since boot
index_result_t index_last_result(void);
int index_progress(void);  // folders crawled so far, -1 when idle

// Holds the index lock until index_release(); may return NULL.
search_index_t *index_acquire(void);
void index_release(void);
// A folder listing from the index, without asking the NAS. False when the index does not know dir.
bool index_list(const char *dir, nas_entry_t **entries, int *count);
