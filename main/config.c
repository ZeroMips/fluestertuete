#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"

#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_partition.h"
#include "esp_rom_crc.h"
#include "flash_guard.h"
#define CRC32(buf, len) esp_rom_crc32_le(0, (buf), (len))
#else
#include <zlib.h>  // host tests
#define CRC32(buf, len) ((uint32_t)crc32(0, (buf), (len)))
#endif

#define PARTITION_SIZE_MAX (16 * 1024)

static config_t s_cfg;

static uint32_t le32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

static void fail(config_t *cfg, config_error_t why)
{
    cfg->valid = false;
    cfg->error = why;
}

static bool get(const cJSON *o, const char *key, char *out, size_t size, bool required)
{
    const cJSON *v = cJSON_GetObjectItem(o, key);
    out[0] = 0;
    if (!cJSON_IsString(v) || !v->valuestring[0]) return !required;
    if (strlen(v->valuestring) >= size) return false;
    strcpy(out, v->valuestring);
    return true;
}

// "https://nas:5001/dav" -> "nas"
static void host_of(const char *url, char *out, size_t size)
{
    const char *p = strstr(url, "://");
    p = p ? p + 3 : url;
    size_t n = strcspn(p, ":/");
    snprintf(out, size, "%.*s", (int)n, p);
}

static void trim_slash(char *s)
{
    size_t n = strlen(s);
    while (n > 1 && s[n - 1] == '/') s[--n] = 0;
}

bool config_parse(const uint8_t *data, size_t len, config_t *cfg)
{
    free(cfg->ca_pem);
    memset(cfg, 0, sizeof(*cfg));
    strcpy(cfg->lang, "de");
    if (len < CONFIG_HEADER_SIZE || memcmp(data, CONFIG_MAGIC, 4) != 0) {
        fail(cfg, CFG_ERR_MISSING);
        return false;
    }
    unsigned version = data[4] | data[5] << 8;
    uint32_t json_len = le32(data + 8), crc = le32(data + 12);
    if (version != CONFIG_FORMAT_VERSION) {
        fail(cfg, CFG_ERR_VERSION);
        return false;
    }
    if (json_len > len - CONFIG_HEADER_SIZE || CRC32(data + CONFIG_HEADER_SIZE, json_len) != crc) {
        fail(cfg, CFG_ERR_DAMAGED);
        return false;
    }
    cJSON *o = cJSON_ParseWithLength((const char *)data + CONFIG_HEADER_SIZE, json_len);
    if (!o) {
        fail(cfg, CFG_ERR_UNREADABLE);
        return false;
    }
    char type[16];
    // The language first: even settings that fail further down should explain that in it.
    if (!get(o, "lang", cfg->lang, sizeof(cfg->lang), false) || (strcmp(cfg->lang, "de") && strcmp(cfg->lang, "en")))
        strcpy(cfg->lang, "de");
    bool ok = get(o, "wifi_ssid", cfg->wifi_ssid, sizeof(cfg->wifi_ssid), true) &&
              get(o, "wifi_password", cfg->wifi_password, sizeof(cfg->wifi_password), false) &&
              get(o, "nas_type", type, sizeof(type), true) &&
              get(o, "nas_url", cfg->nas_url, sizeof(cfg->nas_url), true) &&
              get(o, "tls_name", cfg->tls_name, sizeof(cfg->tls_name), false) &&
              get(o, "root", cfg->root, sizeof(cfg->root), true) &&
              get(o, "share_id", cfg->share_id, sizeof(cfg->share_id), false) &&
              get(o, "share_password", cfg->share_password, sizeof(cfg->share_password), false) &&
              get(o, "dav_user", cfg->dav_user, sizeof(cfg->dav_user), false) &&
              get(o, "dav_password", cfg->dav_password, sizeof(cfg->dav_password), false) &&
              get(o, "ripper_url", cfg->ripper_url, sizeof(cfg->ripper_url), false) &&
              get(o, "ripper_password", cfg->ripper_password, sizeof(cfg->ripper_password), false) &&
              get(o, "boombox_url", cfg->boombox_url, sizeof(cfg->boombox_url), false) &&
              get(o, "dlna_url", cfg->dlna_url, sizeof(cfg->dlna_url), false);
    const cJSON *pem = cJSON_GetObjectItem(o, "ca_pem");
    if (ok && cJSON_IsString(pem) && pem->valuestring[0]) {
#ifdef ESP_PLATFORM
        cfg->ca_pem = heap_caps_malloc(strlen(pem->valuestring) + 1, MALLOC_CAP_SPIRAM);
#else
        cfg->ca_pem = malloc(strlen(pem->valuestring) + 1);
#endif
        if (cfg->ca_pem) strcpy(cfg->ca_pem, pem->valuestring);
    }
    cJSON_Delete(o);
    if (!ok) {
        fail(cfg, CFG_ERR_INCOMPLETE);
        return false;
    }
    if (strcmp(type, "synology") == 0) {
        cfg->nas_type = CFG_NAS_SYNOLOGY;
        if (!cfg->share_id[0]) {
            fail(cfg, CFG_ERR_NO_SHARE);
            return false;
        }
    } else if (strcmp(type, "webdav") == 0) {
        cfg->nas_type = CFG_NAS_WEBDAV;
    } else {
        fail(cfg, CFG_ERR_NAS_TYPE);
        return false;
    }
    trim_slash(cfg->nas_url);
    trim_slash(cfg->root);
    if (cfg->root[0] != '/') {
        fail(cfg, CFG_ERR_ROOT);
        return false;
    }
    host_of(cfg->nas_url, cfg->nas_host, sizeof(cfg->nas_host));
    if (!cfg->tls_name[0]) snprintf(cfg->tls_name, sizeof(cfg->tls_name), "%s", cfg->nas_host);
    cfg->valid = true;
    return true;
}

const config_t *config(void) { return &s_cfg; }

#ifdef ESP_PLATFORM
static void read_partition(void *arg)
{
    uint8_t **buf = arg;
    const esp_partition_t *p = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, 0x41, "config");
    if (!p) return;
    size_t n = p->size < PARTITION_SIZE_MAX ? p->size : PARTITION_SIZE_MAX;
    *buf = heap_caps_malloc(n, MALLOC_CAP_SPIRAM);
    if (*buf && esp_partition_read(p, 0, *buf, n) != ESP_OK) {
        free(*buf);
        *buf = NULL;
    }
}

void config_load(void)
{
    uint8_t *buf = NULL;
    flash_run(read_partition, &buf);
    config_parse(buf ? buf : (const uint8_t *)"", buf ? PARTITION_SIZE_MAX : 0, &s_cfg);
    free(buf);
    if (s_cfg.valid)
        ESP_LOGI("config", "%s at %s, folder %s%s%s", s_cfg.nas_type == CFG_NAS_SYNOLOGY ? "Synology" : "WebDAV",
                 s_cfg.nas_host, s_cfg.root, s_cfg.ripper_url[0] ? ", ripper" : "", s_cfg.boombox_url[0] ? ", boombox" : "");
    else
        ESP_LOGW("config", "not usable (error %d)", s_cfg.error);
}
#endif
