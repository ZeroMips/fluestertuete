// Backend selection and what both backends share. The backends are nas_synology.c and nas_webdav.c.
#include "nas.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "nas_priv.h"
#include "sdkconfig.h"
#include "util.h"

static const nas_backend_t *s_backend;

static const nas_backend_t *backend(void)
{
    if (!s_backend) {
        switch (config()->nas_type) {
#if CONFIG_PLAYER_NAS_SYNOLOGY
        case CFG_NAS_SYNOLOGY: s_backend = &nas_backend_synology; break;
#endif
#if CONFIG_PLAYER_NAS_WEBDAV
        case CFG_NAS_WEBDAV: s_backend = &nas_backend_webdav; break;
#endif
        default: break;
        }
    }
    return s_backend;
}

bool nas_supported(void) { return backend() != NULL; }

void nas_http_config(esp_http_client_config_t *cfg, const char *url)
{
    const config_t *c = config();
    memset(cfg, 0, sizeof(*cfg));
    cfg->url = url;
    cfg->timeout_ms = 15000;
    cfg->buffer_size = 4096;
    cfg->buffer_size_tx = NAS_URL_MAX + 512;
    if (strncmp(c->nas_url, "https://", 8) == 0) {
        if (c->ca_pem) cfg->cert_pem = c->ca_pem;  // own CA or a self-signed NAS certificate
        else cfg->crt_bundle_attach = esp_crt_bundle_attach;
        // Reached by LAN address, but the certificate names another host: verify that name.
        if (strcmp(c->tls_name, c->nas_host) != 0) cfg->common_name = c->tls_name;
    }
}

esp_err_t nas_on_http_event(esp_http_client_event_t *ev)
{
    nas_body_t *b = ev->user_data;
    if (ev->event_id != HTTP_EVENT_ON_DATA || !b || b->failed) return ESP_OK;
    if (b->len + ev->data_len + 1 > b->cap) {
        size_t cap = b->cap ? b->cap : 16384;
        while (cap < b->len + ev->data_len + 1) cap *= 2;
        char *grown = heap_caps_realloc(b->buf, cap, MALLOC_CAP_SPIRAM);
        if (!grown) {
            b->failed = true;
            return ESP_OK;
        }
        b->buf = grown;
        b->cap = cap;
    }
    memcpy(b->buf + b->len, ev->data, ev->data_len);
    b->len += ev->data_len;
    b->buf[b->len] = 0;
    return ESP_OK;
}

char *nas_read_body(esp_http_client_handle_t h)
{
    size_t cap = 16384, len = 0;
    char *buf = heap_caps_malloc(cap, MALLOC_CAP_SPIRAM);
    while (buf) {
        if (len + 1025 > cap) {
            cap *= 2;
            char *grown = heap_caps_realloc(buf, cap, MALLOC_CAP_SPIRAM);
            if (!grown) {
                free(buf);
                return NULL;
            }
            buf = grown;
        }
        int n = esp_http_client_read(h, buf + len, 1024);
        if (n < 0) {
            free(buf);
            return NULL;
        }
        if (n == 0) break;
        len += n;
    }
    if (buf) buf[len] = 0;
    return buf;
}

bool nas_add_entry(nas_entry_t **entries, int *count, int *cap, const char *name, bool is_dir, uint32_t size)
{
    if (is_hidden_name(name)) return true;
    if (*count >= *cap) {
        int n = *cap ? *cap * 2 : 64;
        nas_entry_t *grown = realloc(*entries, sizeof(nas_entry_t) * n);
        if (!grown) return false;
        *entries = grown;
        *cap = n;
    }
    nas_entry_t *e = &(*entries)[*count];
    if (!(e->name = strdup(name))) return false;
    e->is_dir = is_dir;
    e->size = size;
    (*count)++;
    return true;
}

// ---- public API (nas.h)

void nas_init(void) {}

nas_err_t nas_login(void) { return backend() ? backend()->login() : NAS_ERR_API; }

static int entry_cmp(const void *pa, const void *pb)
{
    const nas_entry_t *a = pa, *b = pb;
    if (a->is_dir != b->is_dir) return a->is_dir ? -1 : 1;
    return nat_cmp(a->name, b->name);
}

void nas_sort_entries(nas_entry_t *entries, int count)
{
    if (count > 1) qsort(entries, count, sizeof(*entries), entry_cmp);
}

void nas_free_entries(nas_entry_t *entries, int count)
{
    for (int i = 0; i < count; i++) free(entries[i].name);
    free(entries);
}

nas_lister_t *nas_lister_new(void)
{
    if (!backend()) return NULL;
    nas_lister_t *l = calloc(1, sizeof(*l));
    if (!l || !(l->url = malloc(NAS_URL_MAX)) || !backend()->lister_init(l)) {
        if (l) free(l->url);
        free(l);
        return NULL;
    }
    return l;
}

void nas_lister_free(nas_lister_t *l)
{
    if (!l) return;
    esp_http_client_cleanup(l->http);
    free(l->body.buf);
    free(l->url);
    free(l);
}

nas_err_t nas_list_with(nas_lister_t *l, const char *dir, nas_entry_t **out, int *out_count)
{
    nas_entry_t *entries = NULL;
    int count = 0;
    nas_err_t res = backend()->list(l, dir, &entries, &count);
    if (res != NAS_OK) {
        nas_free_entries(entries, count);
        return res;
    }
    nas_sort_entries(entries, count);
    *out = entries;
    *out_count = count;
    return NAS_OK;
}

nas_err_t nas_list(const char *dir, nas_entry_t **out, int *out_count)
{
    nas_lister_t *l = nas_lister_new();
    if (!l) return NAS_ERR_NET;
    nas_err_t res = nas_list_with(l, dir, out, out_count);
    nas_lister_free(l);
    return res;
}

nas_err_t nas_stream_open(const char *path, uint32_t offset, nas_stream_t **out)
{
    return backend() ? backend()->stream_open(path, offset, out) : NAS_ERR_API;
}

int nas_stream_read(nas_stream_t *s, uint8_t *buf, size_t n) { return esp_http_client_read(s->http, (char *)buf, (int)n); }

void nas_stream_close(nas_stream_t *s)
{
    esp_http_client_close(s->http);
    esp_http_client_cleanup(s->http);
    free(s);
}
