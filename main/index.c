#include "index.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dlna.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_partition.h"
#include "esp_rom_crc.h"
#include "flash_guard.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nas.h"
#include "util.h"

static const char *TAG = "index";

#define MAGIC 0x32584454  // "TDX2": tracks carry their size
#define REFRESH_DELAY_MS 15000
#define REFRESH_PERIOD_MS (3 * 60 * 60 * 1000)
#define DLNA_POLL_MS 60000
#define DLNA_SETTLE_US (120 * 1000000LL)  // copying an album bumps the id many times: wait until it rests
#define MAX_DEPTH 8

typedef struct {
    uint32_t magic, len, crc;
} header_t;

typedef struct {
    char *buf;
    size_t len, cap;
    bool failed;
} text_t;

static SemaphoreHandle_t s_lock;
static search_index_t *s_index;
static volatile int s_progress = -1;
static const esp_partition_t *s_part;
static TaskHandle_t s_task;
static volatile uint32_t s_crawls;
static volatile index_result_t s_result = INDEX_NONE;

search_index_t *index_acquire(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    return s_index;
}

void index_release(void) { xSemaphoreGive(s_lock); }
int index_progress(void) { return s_progress; }

static void install(char *text, size_t len)
{
    search_index_t *idx = search_index_parse(text, len);
    if (!idx) {
        free(text);
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    search_index_t *old = s_index;
    s_index = idx;
    xSemaphoreGive(s_lock);
    search_index_free(old);
    ESP_LOGI(TAG, "%d entries, internal RAM free %u", search_index_count(idx), (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
}

void index_init(void)
{
    s_lock = xSemaphoreCreateMutex();
    s_part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, 0x40, "index");
    header_t h;
    if (!s_part || esp_partition_read(s_part, 0, &h, sizeof(h)) != ESP_OK || h.magic != MAGIC ||
        h.len > s_part->size - sizeof(h)) {
        ESP_LOGI(TAG, "no cached index");
        return;
    }
    char *text = heap_caps_malloc(h.len + 1, MALLOC_CAP_SPIRAM);
    if (!text) return;
    if (esp_partition_read(s_part, sizeof(h), text, h.len) != ESP_OK || esp_rom_crc32_le(0, (uint8_t *)text, h.len) != h.crc) {
        free(text);
        ESP_LOGW(TAG, "cached index damaged");
        return;
    }
    text[h.len] = 0;
    install(text, h.len);
}

typedef struct {
    const char *text;
    size_t len;
    header_t *h;
    bool ok;
} flash_job_t;

static void save_now(void *arg)
{
    flash_job_t *j = arg;
    const char *text = j->text;
    size_t len = j->len;
    if (!s_part || len + sizeof(header_t) > s_part->size) return;
    header_t h = {.magic = MAGIC, .len = len, .crc = esp_rom_crc32_le(0, (const uint8_t *)text, len)};
    size_t erase = (len + sizeof(h) + 4095) & ~(size_t)4095;
    if (esp_partition_erase_range(s_part, 0, erase) == ESP_OK && esp_partition_write(s_part, sizeof(h), text, len) == ESP_OK)
        esp_partition_write(s_part, 0, &h, sizeof(h));  // header last: a cut write leaves no valid index
}

static void read_header(void *arg)
{
    flash_job_t *j = arg;
    j->ok = s_part && esp_partition_read(s_part, 0, j->h, sizeof(*j->h)) == ESP_OK;
}

// Flash access goes through flash_run(): the crawl task's stack may live in PSRAM.
static void save(const char *text, size_t len)
{
    flash_job_t j = {.text = text, .len = len};
    flash_run(save_now, &j);
}

static void append(text_t *t, char kind, const char *dir, const char *name, uint32_t size)
{
    size_t need = 1 + strlen(dir) + (name ? strlen(name) + 1 + 12 : 0) + 2;
    if (t->len + need > t->cap) {
        size_t cap = t->cap ? t->cap * 2 : 65536;
        while (cap < t->len + need) cap *= 2;
        char *grown = heap_caps_realloc(t->buf, cap, MALLOC_CAP_SPIRAM);
        if (!grown) {
            t->failed = true;
            return;
        }
        t->buf = grown;
        t->cap = cap;
    }
    if (name) t->len += sprintf(t->buf + t->len, "%c%s/%s\t%lu\n", kind, dir, name, (unsigned long)size);
    else t->len += sprintf(t->buf + t->len, "%c%s\n", kind, dir);
}

static void crawl(nas_lister_t *l, const char *dir, text_t *out, int depth)
{
    nas_entry_t *entries = NULL;
    int count = 0;
    nas_err_t err = NAS_ERR_NET;
    for (int attempt = 0; attempt < 3 && err != NAS_OK; attempt++) {
        if (attempt) vTaskDelay(pdMS_TO_TICKS(2000));
        err = nas_list_with(l, dir, &entries, &count);
    }
    if (err != NAS_OK) {
        out->failed = true;  // an incomplete crawl must not replace a complete index
        return;
    }
    s_progress++;
    int audio = 0;
    for (int i = 0; i < count; i++) audio += !entries[i].is_dir && is_audio_name(entries[i].name);
    if (depth) append(out, audio ? 'A' : 'S', dir, NULL, 0);
    for (int i = 0; i < count; i++)
        if (!entries[i].is_dir && is_audio_name(entries[i].name)) append(out, 'T', dir, entries[i].name, entries[i].size);
    char *sub = malloc(NAS_PATH_MAX);
    for (int i = 0; sub && i < count && !out->failed && depth < MAX_DEPTH; i++) {
        if (!entries[i].is_dir) continue;
        if (snprintf(sub, NAS_PATH_MAX, "%s/%s", dir, entries[i].name) < NAS_PATH_MAX) crawl(l, sub, out, depth + 1);
    }
    free(sub);
    nas_free_entries(entries, count);
}

static index_result_t crawl_once(void)
{
    nas_lister_t *l = nas_lister_new();
    text_t t = {0};
    s_progress = 0;
    if (l) crawl(l, NAS_ROOT, &t, 0);
    nas_lister_free(l);
    ESP_LOGI(TAG, "crawled %d folders%s", s_progress, t.failed ? ", incomplete" : "");
    s_progress = -1;
    if (!l || t.failed || !t.len) {
        free(t.buf);
        return INDEX_FAILED;
    }
    // Rewrite flash only when the library changed.
    bool same = false;
    header_t h;
    uint32_t crc = esp_rom_crc32_le(0, (uint8_t *)t.buf, t.len);
    flash_job_t j = {.h = &h};
    flash_run(read_header, &j);
    if (j.ok) same = h.magic == MAGIC && h.len == t.len && h.crc == crc;
    if (same && s_index) {
        free(t.buf);
        ESP_LOGI(TAG, "unchanged");
        return INDEX_UNCHANGED;
    }
    save(t.buf, t.len);
    install(t.buf, t.len);
    return INDEX_UPDATED;
}

static void refresh_task(void *arg)
{
    // Shortly after boot, then when Media Server reports a change, every few hours, or at once when asked.
    uint32_t base = 0, seen = 0, id;
    bool have_base = false;
    int64_t changed_at = 0;
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(REFRESH_DELAY_MS));
    for (;;) {
        // The id before the crawl: whatever changes during it shows up as a newer id afterwards.
        if (dlna_system_update_id(&id)) {
            if (!have_base) ESP_LOGI(TAG, "media server update id %u", (unsigned)id);
            base = seen = id;
            have_base = true;
        }
        s_result = crawl_once();
        s_crawls++;
        ulTaskNotifyTake(pdTRUE, 0);  // a request made during this crawl is answered by it

        int64_t next_full = esp_timer_get_time() + (int64_t)REFRESH_PERIOD_MS * 1000;
        for (;;) {
            int64_t now = esp_timer_get_time();
            if (now >= next_full) break;
            int64_t wait_ms = (next_full - now) / 1000;
            if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(wait_ms < DLNA_POLL_MS ? wait_ms : DLNA_POLL_MS))) break;
            if (!dlna_system_update_id(&id)) continue;
            now = esp_timer_get_time();
            if (!have_base) {  // Media Server was down at the last crawl: start watching from here
                base = seen = id;
                have_base = true;
            }
            if (id != seen) {
                seen = id;
                changed_at = now;
            }
            if (id != base && now - changed_at >= DLNA_SETTLE_US) {
                ESP_LOGI(TAG, "media server changed (%u -> %u)", (unsigned)base, (unsigned)id);
                break;
            }
        }
    }
}

void index_request_refresh(void)
{
    if (s_task && s_progress < 0) xTaskNotifyGive(s_task);
}

uint32_t index_crawls(void) { return s_crawls; }
index_result_t index_last_result(void) { return s_result; }

bool index_list(const char *dir, nas_entry_t **out, int *out_count)
{
    search_index_t *idx = index_acquire();
    int n = idx ? search_index_children(idx, dir, NULL, 0) : -1;
    if (n < 0) {
        index_release();
        return false;
    }
    // Count first, then copy: the index only answers with a bounded buffer.
    n = 0;
    for (int max = 64;; max *= 2) {
        search_child_t *kids = malloc(sizeof(*kids) * max);
        if (!kids) break;
        int got = search_index_children(idx, dir, kids, max);
        if (got == max) {
            free(kids);
            continue;
        }
        nas_entry_t *entries = calloc(got + 1, sizeof(*entries));
        for (int i = 0; entries && i < got; i++) {
            entries[i].name = strdup(kids[i].name);
            entries[i].size = kids[i].size;
            entries[i].is_dir = kids[i].kind != SEARCH_TRACK;
        }
        free(kids);
        if (!entries) break;
        index_release();
        nas_sort_entries(entries, got);
        *out = entries;
        *out_count = got;
        return true;
    }
    index_release();
    return false;
}

void index_start_refresh(void)
{
#if CONFIG_SPIRAM_ALLOW_STACK_EXTERNAL_MEMORY
    xTaskCreatePinnedToCoreWithCaps(refresh_task, "index", 10240, NULL, 3, &s_task, 0, MALLOC_CAP_SPIRAM);
#else
    xTaskCreatePinnedToCore(refresh_task, "index", 10240, NULL, 3, &s_task, 0);
#endif
}
