#include "dlna.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_http_client.h"
#include "config.h"

// Synology Media Server answers on this fixed port; it does not announce itself via SSDP. Other NAS types only
// when the setup names a ContentDirectory control URL.
static const char *control_url(void)
{
    static char url[160];
    const config_t *c = config();
    if (!url[0]) {
        if (c->dlna_url[0]) snprintf(url, sizeof(url), "%s", c->dlna_url);
        else if (c->nas_type == CFG_NAS_SYNOLOGY) snprintf(url, sizeof(url), "http://%s:50001/ContentDirectory/control", c->nas_host);
    }
    return url;
}
#define ACTION "urn:schemas-upnp-org:service:ContentDirectory:1#GetSystemUpdateID"
#define BODY                                                                                                      \
    "<?xml version=\"1.0\"?><s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "                  \
    "s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\"><s:Body>"                                     \
    "<u:GetSystemUpdateID xmlns:u=\"urn:schemas-upnp-org:service:ContentDirectory:1\"/></s:Body></s:Envelope>"

bool dlna_system_update_id(uint32_t *id)
{
    if (!control_url()[0]) return false;
    esp_http_client_config_t cfg = {
        .url = control_url(),
        .method = HTTP_METHOD_POST,
        .timeout_ms = 3000,
        .disable_auto_redirect = true,
    };
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    if (!c) return false;
    esp_http_client_set_header(c, "Content-Type", "text/xml; charset=\"utf-8\"");
    esp_http_client_set_header(c, "SOAPACTION", "\"" ACTION "\"");
    char buf[640];
    int len = 0;
    bool ok = false;
    if (esp_http_client_open(c, strlen(BODY)) == ESP_OK && esp_http_client_write(c, BODY, strlen(BODY)) == (int)strlen(BODY)) {
        esp_http_client_fetch_headers(c);
        if (esp_http_client_get_status_code(c) == 200) {
            int n;
            while (len < (int)sizeof(buf) - 1 && (n = esp_http_client_read(c, buf + len, sizeof(buf) - 1 - len)) > 0) len += n;
            buf[len] = 0;
            const char *p = strstr(buf, "<Id>");
            if (p) {
                char *end;
                unsigned long v = strtoul(p + 4, &end, 10);
                if (end != p + 4 && *end == '<') {
                    *id = (uint32_t)v;
                    ok = true;
                }
            }
        }
    }
    esp_http_client_cleanup(c);
    return ok;
}
