#include "store.h"

#include <stdio.h>
#include <string.h>

#include "nvs.h"
#include "nvs_flash.h"
#include "flash_guard.h"
#include "util.h"

static nvs_handle_t s_nvs;


void store_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
    ESP_ERROR_CHECK(nvs_open("player", NVS_READWRITE, &s_nvs));
}

static void pos_key(const char *album, char key[16]) { snprintf(key, 16, "p%08lx", (unsigned long)fnv1a(album)); }

static bool store_get_pos_now(const char *album, store_pos_t *pos)
{
    char key[16];
    size_t len = sizeof(*pos);
    pos_key(album, key);
    return nvs_get_blob(s_nvs, key, pos, &len) == ESP_OK && len == sizeof(*pos);
}

static void store_set_pos_now(const char *album, const store_pos_t *pos)
{
    char key[16];
    pos_key(album, key);
    nvs_set_blob(s_nvs, key, pos, sizeof(*pos));
    nvs_commit(s_nvs);
}

static void store_clear_pos_now(const char *album)
{
    char key[16];
    pos_key(album, key);
    nvs_erase_key(s_nvs, key);
    nvs_commit(s_nvs);
}

static int store_get_volume_now(int fallback)
{
    int32_t v;
    return nvs_get_i32(s_nvs, "volume", &v) == ESP_OK ? (int)v : fallback;
}

static void store_set_volume_now(int volume)
{
    nvs_set_i32(s_nvs, "volume", volume);
    nvs_commit(s_nvs);
}

static bool store_get_boombox_now(void)
{
    uint8_t v = 0;
    return nvs_get_u8(s_nvs, "boombox", &v) == ESP_OK && v;
}

static void store_set_boombox_now(bool on)
{
    nvs_set_u8(s_nvs, "boombox", on);
    nvs_commit(s_nvs);
}

static bool store_get_last_now(char *path, size_t size) { return nvs_get_str(s_nvs, "last", path, &size) == ESP_OK; }

static void store_set_last_now(const char *path)
{
    nvs_set_str(s_nvs, "last", path);
    nvs_commit(s_nvs);
}

// Public entry points: each runs where flash access is allowed (see flash_guard.h).
typedef struct {
    const char *str;
    char *out;
    size_t size;
    const store_pos_t *pos_in;
    store_pos_t *pos_out;
    int num;
    bool ok;
} call_t;

static void get_pos(void *a) { call_t *c = a; c->ok = store_get_pos_now(c->str, c->pos_out); }
static void set_pos(void *a) { call_t *c = a; store_set_pos_now(c->str, c->pos_in); }
static void clear_pos(void *a) { call_t *c = a; store_clear_pos_now(c->str); }
static void get_volume(void *a) { call_t *c = a; c->num = store_get_volume_now(c->num); }
static void set_volume(void *a) { call_t *c = a; store_set_volume_now(c->num); }
static void get_boombox(void *a) { call_t *c = a; c->ok = store_get_boombox_now(); }
static void set_boombox(void *a) { call_t *c = a; store_set_boombox_now(c->ok); }
static void get_last(void *a) { call_t *c = a; c->ok = store_get_last_now(c->out, c->size); }
static void set_last(void *a) { call_t *c = a; store_set_last_now(c->str); }

bool store_get_pos(const char *album, store_pos_t *pos)
{
    call_t c = {.str = album, .pos_out = pos};
    flash_run(get_pos, &c);
    return c.ok;
}

void store_set_pos(const char *album, const store_pos_t *pos)
{
    call_t c = {.str = album, .pos_in = pos};
    flash_run(set_pos, &c);
}

void store_clear_pos(const char *album)
{
    call_t c = {.str = album};
    flash_run(clear_pos, &c);
}

int store_get_volume(int fallback)
{
    call_t c = {.num = fallback};
    flash_run(get_volume, &c);
    return c.num;
}

void store_set_volume(int volume)
{
    call_t c = {.num = volume};
    flash_run(set_volume, &c);
}

bool store_get_last(char *path, size_t size)
{
    call_t c = {.out = path, .size = size};
    flash_run(get_last, &c);
    return c.ok;
}

void store_set_last(const char *path)
{
    call_t c = {.str = path};
    flash_run(set_last, &c);
}

bool store_get_boombox(void)
{
    call_t c = {0};
    flash_run(get_boombox, &c);
    return c.ok;
}

void store_set_boombox(bool on)
{
    call_t c = {.ok = on};
    flash_run(set_boombox, &c);
}
