// The device's settings: Wi-Fi, NAS, optional CD ripper and boombox. They live in the "config" partition, written
// by the setup website (web/) or tools/make_config.py, so one firmware serves everyone and a settings change keeps
// the listening positions in nvs. Format: 16-byte header (magic "FLUE", u16 version, u16 reserved, u32 length,
// u32 CRC-32 of the JSON), then UTF-8 JSON.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CONFIG_MAGIC "FLUE"
#define CONFIG_FORMAT_VERSION 1
#define CONFIG_HEADER_SIZE 16

typedef enum { CFG_NAS_SYNOLOGY, CFG_NAS_WEBDAV } cfg_nas_type_t;

// Why the settings are not usable; same order as S_CFG_MISSING... in i18n.h.
typedef enum {
    CFG_OK,
    CFG_ERR_MISSING,
    CFG_ERR_VERSION,
    CFG_ERR_DAMAGED,
    CFG_ERR_UNREADABLE,
    CFG_ERR_INCOMPLETE,
    CFG_ERR_NO_SHARE,
    CFG_ERR_NAS_TYPE,
    CFG_ERR_ROOT,
} config_error_t;

typedef struct {
    bool valid;
    config_error_t error;  // why not valid
    char lang[4];          // "de" | "en"; "de" when not set

    char wifi_ssid[33], wifi_password[65];

    cfg_nas_type_t nas_type;
    char nas_url[160];   // https://host:port, for WebDAV with the path of the DAV root
    char nas_host[96];   // host part of nas_url
    char tls_name[96];   // name the certificate must carry; "" = the host of nas_url
    char *ca_pem;        // optional own CA or self-signed certificate; NULL = public CAs
    char root[256];      // the folder with the audio dramas, as the NAS sees it, e.g. "/Hörspiele für Kinder"
    char share_id[64], share_password[128];  // Synology share link
    char dav_user[64], dav_password[128];    // WebDAV

    char ripper_url[128], ripper_password[64];  // optional: CD ripper (cd-web)
    char boombox_url[128];                      // optional: Bluetooth speaker at the ripper (cd-bt)
    char dlna_url[160];                         // optional: media server change signal
} config_t;

// Reads the partition once at boot. Afterwards config() is constant.
void config_load(void);
const config_t *config(void);

// Pure parsing, shared with the host tests: header + JSON -> cfg. Returns cfg->valid.
bool config_parse(const uint8_t *data, size_t len, config_t *cfg);
