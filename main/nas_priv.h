// Shared between nas.c and the backends (nas_synology.c, nas_webdav.c). Not for other modules.
#pragma once
#include "esp_http_client.h"
#include "nas.h"

typedef struct {
    char *buf;
    size_t len, cap;
    bool failed;
} nas_body_t;

struct nas_stream {
    esp_http_client_handle_t http;
};

struct nas_lister {
    esp_http_client_handle_t http;  // kept open across listings
    nas_body_t body;
    char *url;  // NAS_URL_MAX
};

#define NAS_URL_MAX 4096

typedef struct {
    nas_err_t (*login)(void);
    bool (*lister_init)(nas_lister_t *l);  // creates l->http
    // Entries of dir in any order, hidden ones left out.
    nas_err_t (*list)(nas_lister_t *l, const char *dir, nas_entry_t **entries, int *count);
    nas_err_t (*stream_open)(const char *path, uint32_t offset, nas_stream_t **out);
} nas_backend_t;

extern const nas_backend_t nas_backend_synology, nas_backend_webdav;

// HTTP client settings every NAS request shares: TLS verification as configured, timeouts, buffers.
void nas_http_config(esp_http_client_config_t *cfg, const char *url);
esp_err_t nas_on_http_event(esp_http_client_event_t *ev);  // collects a body into user_data (nas_body_t)
char *nas_read_body(esp_http_client_handle_t h);           // whole response, NUL-terminated, in PSRAM
bool nas_add_entry(nas_entry_t **entries, int *count, int *cap, const char *name, bool is_dir, uint32_t size);
