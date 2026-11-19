// WebDAV: PROPFIND (Depth: 1) for listings, GET with Range for streaming, HTTP Basic auth. Works with the
// WebDAV servers of Synology, QNAP, Nextcloud, Apache mod_dav, rclone and others.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "davxml.h"
#include "esp_log.h"
#include "nas_priv.h"
#include "util.h"

static const char *TAG = "dav";

static const char PROPFIND_BODY[] = "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
                                    "<d:propfind xmlns:d=\"DAV:\"><d:prop><d:resourcetype/><d:getcontentlength/></d:prop></d:propfind>";

// Full URL of a path below the DAV root. Returns false when it does not fit.
static bool make_url(const char *path, char *url, size_t size)
{
    int n = snprintf(url, size, "%s", config()->nas_url);
    return n > 0 && (size_t)n < size && url_encode_path(path, url + n, size - n) > 0;
}

// The path part of a URL, for recognising the folder's own entry in a listing.
static const char *url_path(const char *url)
{
    const char *p = strstr(url, "://");
    p = p ? strchr(p + 3, '/') : NULL;
    return p ? p : "/";
}

static void set_auth(esp_http_client_config_t *cfg)
{
    const config_t *c = config();
    if (!c->dav_user[0]) return;
    cfg->username = c->dav_user;
    cfg->password = c->dav_password;
    cfg->auth_type = HTTP_AUTH_TYPE_BASIC;  // sent with the first request, no 401 round trip
}

static nas_err_t status_error(int status)
{
    if (status == 401 || status == 403) return NAS_ERR_AUTH;
    if (status >= 400 && status < 500) return NAS_ERR_API;
    return NAS_ERR_NET;
}

static bool lister_init(nas_lister_t *l)
{
    esp_http_client_config_t cfg;
    nas_http_config(&cfg, config()->nas_url);
    set_auth(&cfg);
    cfg.keep_alive_enable = true;
    cfg.event_handler = nas_on_http_event;
    cfg.user_data = &l->body;
    l->http = esp_http_client_init(&cfg);
    return l->http != NULL;
}

// One PROPFIND on the lister's connection; the answer lands in l->body.
static nas_err_t propfind(nas_lister_t *l, const char *path, const char *depth)
{
    if (!make_url(path, l->url, NAS_URL_MAX)) return NAS_ERR_API;
    esp_http_client_set_url(l->http, l->url);
    esp_http_client_set_method(l->http, HTTP_METHOD_PROPFIND);
    esp_http_client_set_header(l->http, "Depth", depth);
    esp_http_client_set_header(l->http, "Content-Type", "application/xml; charset=utf-8");
    esp_http_client_set_post_field(l->http, PROPFIND_BODY, sizeof(PROPFIND_BODY) - 1);
    l->body.len = 0;
    l->body.failed = false;
    if (esp_http_client_perform(l->http) != ESP_OK || l->body.failed) return NAS_ERR_NET;
    int status = esp_http_client_get_status_code(l->http);
    if (status != 207) {
        ESP_LOGW(TAG, "PROPFIND %s: HTTP %d", path, status);
        return status_error(status);
    }
    return l->body.len ? NAS_OK : NAS_ERR_NET;
}

static nas_err_t login(void)
{
    // No session: checking that the folder answers proves address, login and path at once.
    nas_lister_t *l = nas_lister_new();
    if (!l) return NAS_ERR_NET;
    nas_err_t res = propfind(l, config()->root, "0");
    nas_lister_free(l);
    if (res == NAS_OK) ESP_LOGI(TAG, "folder reachable");
    return res;
}

typedef struct {
    nas_entry_t **entries;
    int *count, cap;
    bool failed;
} collect_t;

static void on_entry(const char *name, bool is_dir, uint32_t size, void *ctx)
{
    collect_t *c = ctx;
    if (!c->failed && !nas_add_entry(c->entries, c->count, &c->cap, name, is_dir, size)) c->failed = true;
}

static nas_err_t list(nas_lister_t *l, const char *dir, nas_entry_t **entries, int *count)
{
    nas_err_t res = propfind(l, dir, "1");
    if (res != NAS_OK) return res;
    collect_t c = {.entries = entries, .count = count};
    if (davxml_parse(l->body.buf, url_path(l->url), on_entry, &c) < 0) {
        ESP_LOGW(TAG, "%s: no WebDAV answer", dir);
        return NAS_ERR_API;
    }
    return c.failed ? NAS_ERR_NET : NAS_OK;
}

static nas_err_t stream_open(const char *path, uint32_t offset, nas_stream_t **out)
{
    char *url = malloc(NAS_URL_MAX);
    if (!url || !make_url(path, url, NAS_URL_MAX)) {
        free(url);
        return NAS_ERR_API;
    }
    esp_http_client_config_t cfg;
    nas_http_config(&cfg, url);
    set_auth(&cfg);
    esp_http_client_handle_t h = esp_http_client_init(&cfg);
    free(url);
    if (!h) return NAS_ERR_NET;
    char range[32];
    snprintf(range, sizeof(range), "bytes=%lu-", (unsigned long)offset);
    esp_http_client_set_header(h, "Range", range);

    nas_err_t res = NAS_ERR_NET;
    if (esp_http_client_open(h, 0) == ESP_OK && esp_http_client_fetch_headers(h) >= 0) {
        int status = esp_http_client_get_status_code(h);
        if (status == 206 || (status == 200 && offset == 0)) {  // a server without ranges is fine from the start
            nas_stream_t *s = malloc(sizeof(*s));
            if (s) {
                s->http = h;
                *out = s;
                return NAS_OK;
            }
        } else {
            ESP_LOGW(TAG, "GET %s: HTTP %d", path, status);
            res = status_error(status);
        }
    }
    esp_http_client_cleanup(h);
    return res;
}

const nas_backend_t nas_backend_webdav = {login, lister_init, list, stream_open};
