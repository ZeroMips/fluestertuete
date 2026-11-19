// Synology File Station share link: login with the link's password (no account password on the device), then
// listing and ranged download through the FolderSharing API.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nas_priv.h"
#include "util.h"

static const char *TAG = "nas";

#define SID_MAX 256
#define PAGE_SIZE 1000

static SemaphoreHandle_t s_lock;
static char s_sid[SID_MAX];
static char s_base[200];  // https://host:port/webapi/entry.cgi

static const char *base_url(void)
{
    if (!s_base[0]) snprintf(s_base, sizeof(s_base), "%s/webapi/entry.cgi", config()->nas_url);
    return s_base;
}

static SemaphoreHandle_t lock(void)
{
    if (!s_lock) s_lock = xSemaphoreCreateMutex();  // first use is at boot, before any second task runs
    return s_lock;
}

static void get_sid(char *out)
{
    xSemaphoreTake(lock(), portMAX_DELAY);
    strlcpy(out, s_sid, SID_MAX);
    xSemaphoreGive(s_lock);
}

static esp_http_client_handle_t client_new(const char *url, esp_http_client_method_t method)
{
    esp_http_client_config_t cfg;
    nas_http_config(&cfg, url);
    cfg.method = method;
    esp_http_client_handle_t h = esp_http_client_init(&cfg);
    char sid[SID_MAX], cookie[SID_MAX + 16];
    get_sid(sid);
    if (h && sid[0]) {
        snprintf(cookie, sizeof(cookie), "sharing_sid=%s", sid);
        esp_http_client_set_header(h, "Cookie", cookie);
    }
    return h;
}

static bool is_auth_error(int code) { return code == 105 || code == 106 || code == 107 || code == 119 || code == 122; }

static int json_error_code(const cJSON *root)
{
    const cJSON *code = cJSON_GetObjectItem(cJSON_GetObjectItem(root, "error"), "code");
    return cJSON_IsNumber(code) ? code->valueint : -1;
}

static nas_err_t login(void)
{
    const config_t *c = config();
    char id[192], pass[384], body[640];
    if (!url_encode(c->share_id, id, sizeof(id)) || !url_encode(c->share_password, pass, sizeof(pass))) return NAS_ERR_AUTH;
    int len = snprintf(body, sizeof(body), "api=SYNO.Core.Sharing.Login&version=1&method=login&sharing_id=%s&password=%s", id, pass);

    nas_err_t res = NAS_ERR_NET;
    esp_http_client_handle_t h = client_new(base_url(), HTTP_METHOD_POST);
    if (!h) return res;
    esp_http_client_set_header(h, "Content-Type", "application/x-www-form-urlencoded");
    if (esp_http_client_open(h, len) == ESP_OK && esp_http_client_write(h, body, len) == len &&
        esp_http_client_fetch_headers(h) >= 0) {
        char *resp = nas_read_body(h);
        cJSON *root = resp ? cJSON_Parse(resp) : NULL;
        const cJSON *sid = cJSON_GetObjectItem(cJSON_GetObjectItem(root, "data"), "sharing_sid");
        if (cJSON_IsString(sid)) {
            xSemaphoreTake(lock(), portMAX_DELAY);
            strlcpy(s_sid, sid->valuestring, sizeof(s_sid));
            xSemaphoreGive(s_lock);
            ESP_LOGI(TAG, "logged in");
            res = NAS_OK;
        } else if (root) {
            ESP_LOGE(TAG, "login refused, code %d", json_error_code(root));
            res = NAS_ERR_AUTH;
        }
        cJSON_Delete(root);
        free(resp);
    }
    esp_http_client_cleanup(h);
    return res;
}

// A second task may have renewed the session already.
static nas_err_t relogin(const char *failed_sid)
{
    char now[SID_MAX];
    get_sid(now);
    return strcmp(now, failed_sid) ? NAS_OK : login();
}

static bool lister_init(nas_lister_t *l)
{
    esp_http_client_config_t cfg;
    nas_http_config(&cfg, base_url());
    cfg.keep_alive_enable = true;
    cfg.event_handler = nas_on_http_event;
    cfg.user_data = &l->body;
    l->http = esp_http_client_init(&cfg);
    return l->http != NULL;
}

// Fetches one page of a listing. *total is the folder's entry count.
static nas_err_t list_page(nas_lister_t *l, const char *enc_dir, int offset, nas_entry_t **entries, int *count,
                           int *cap, int *total)
{
    char sid[SID_MAX], cookie[SID_MAX + 16], id[192];
    url_encode(config()->share_id, id, sizeof(id));
    snprintf(l->url, NAS_URL_MAX,
             "%s?api=SYNO.FolderSharing.List&version=2&method=list&_sharing_id=%s"
             "&additional=%%5B%%22size%%22%%5D&limit=%d&offset=%d&folder_path=%s",
             base_url(), id, PAGE_SIZE, offset, enc_dir);
    get_sid(sid);
    snprintf(cookie, sizeof(cookie), "sharing_sid=%s", sid);
    esp_http_client_set_url(l->http, l->url);
    esp_http_client_set_header(l->http, "Cookie", cookie);
    l->body.len = 0;
    l->body.failed = false;
    if (esp_http_client_perform(l->http) != ESP_OK || l->body.failed || !l->body.len) return NAS_ERR_NET;

    nas_err_t res = NAS_ERR_NET;
    cJSON *root = cJSON_Parse(l->body.buf);
    if (root) {
        const cJSON *data = cJSON_GetObjectItem(root, "data");
        const cJSON *files = cJSON_GetObjectItem(data, "files");
        const cJSON *tot = cJSON_GetObjectItem(data, "total");
        if (cJSON_IsTrue(cJSON_GetObjectItem(root, "success")) && cJSON_IsArray(files)) {
            res = NAS_OK;
            const cJSON *it;
            cJSON_ArrayForEach(it, files) {
                const cJSON *name = cJSON_GetObjectItem(it, "name");
                if (!cJSON_IsString(name)) continue;
                const cJSON *size = cJSON_GetObjectItem(cJSON_GetObjectItem(it, "additional"), "size");
                if (!nas_add_entry(entries, count, cap, name->valuestring, cJSON_IsTrue(cJSON_GetObjectItem(it, "isdir")),
                                   cJSON_IsNumber(size) ? (uint32_t)size->valuedouble : 0)) {
                    res = NAS_ERR_NET;
                    break;
                }
            }
            *total = cJSON_IsNumber(tot) ? tot->valueint : 0;
        } else {
            int code = json_error_code(root);
            ESP_LOGW(TAG, "list failed, code %d", code);
            res = is_auth_error(code) ? NAS_ERR_AUTH : NAS_ERR_API;
        }
    }
    cJSON_Delete(root);
    return res;
}

static nas_err_t list(nas_lister_t *l, const char *dir, nas_entry_t **entries, int *count)
{
    char *enc = malloc(NAS_PATH_MAX * 3 + 1);
    if (!enc || !url_encode(dir, enc, NAS_PATH_MAX * 3 + 1)) {
        free(enc);
        return NAS_ERR_API;
    }
    char sid[SID_MAX];
    int cap = 0, offset = 0, total = 0, attempts = 0;
    nas_err_t res;
    for (;;) {
        get_sid(sid);
        res = list_page(l, enc, offset, entries, count, &cap, &total);
        if (res == NAS_ERR_AUTH && attempts++ < 2 && relogin(sid) == NAS_OK) continue;
        if (res != NAS_OK) break;
        offset += PAGE_SIZE;
        if (offset >= total) break;
    }
    free(enc);
    return res;
}

static nas_err_t stream_try(const char *enc_path, uint32_t offset, nas_stream_t **out)
{
    char *url = malloc(NAS_URL_MAX), id[192];
    if (!url) return NAS_ERR_NET;
    url_encode(config()->share_id, id, sizeof(id));
    snprintf(url, NAS_URL_MAX, "%s?api=SYNO.FolderSharing.Download&version=2&method=download&mode=open&_sharing_id=%s&path=%s",
             base_url(), id, enc_path);
    esp_http_client_handle_t h = client_new(url, HTTP_METHOD_GET);
    free(url);
    if (!h) return NAS_ERR_NET;
    char range[32];
    snprintf(range, sizeof(range), "bytes=%lu-", (unsigned long)offset);
    esp_http_client_set_header(h, "Range", range);

    nas_err_t res = NAS_ERR_NET;
    if (esp_http_client_open(h, 0) == ESP_OK && esp_http_client_fetch_headers(h) >= 0) {
        int status = esp_http_client_get_status_code(h);
        if (status == 206) {
            nas_stream_t *s = malloc(sizeof(*s));
            if (s) {
                s->http = h;
                *out = s;
                return NAS_OK;
            }
        } else {
            // Errors come as JSON with HTTP status 200.
            char *resp = nas_read_body(h);
            cJSON *root = resp ? cJSON_Parse(resp) : NULL;
            int code = root ? json_error_code(root) : -1;
            ESP_LOGW(TAG, "download: HTTP %d, code %d", status, code);
            res = is_auth_error(code) ? NAS_ERR_AUTH : NAS_ERR_API;
            cJSON_Delete(root);
            free(resp);
        }
    }
    esp_http_client_cleanup(h);
    return res;
}

static nas_err_t stream_open(const char *path, uint32_t offset, nas_stream_t **out)
{
    // The download API wants the path as a JSON array.
    size_t enc_size = NAS_PATH_MAX * 6 + 16;
    char *json = malloc(NAS_PATH_MAX * 2 + 8), *enc = malloc(enc_size);
    nas_err_t res = NAS_ERR_API;
    if (json && enc && json_string_array(path, json, NAS_PATH_MAX * 2 + 8) && url_encode(json, enc, enc_size)) {
        char sid[SID_MAX];
        get_sid(sid);
        res = stream_try(enc, offset, out);
        if (res == NAS_ERR_AUTH && relogin(sid) == NAS_OK) res = stream_try(enc, offset, out);
    }
    free(json);
    free(enc);
    return res;
}

const nas_backend_t nas_backend_synology = {login, lister_init, list, stream_open};
