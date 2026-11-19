#include "boombox.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "config.h"
#include "i18n.h"

static const char *TAG = "boombox";


#define BODY_MAX 2048

static void copy_str(char *out, size_t size, const cJSON *item)
{
    snprintf(out, size, "%s", cJSON_IsString(item) ? item->valuestring : "");
}

// One request. Returns the HTTP status, -1 when the Pi does not answer; the answer lands in body.
static int request(const char *path, const char *post, char *body)
{
    char url[192];
    snprintf(url, sizeof(url), "%s%s", config()->boombox_url, path);
    esp_http_client_config_t cfg = {
        .url = url,
        .method = post ? HTTP_METHOD_POST : HTTP_METHOD_GET,
        .timeout_ms = post ? 25000 : 4000,  // play may wait for the speaker to connect
        .disable_auto_redirect = true,
    };
    if (config()->ripper_password[0]) {  // cd-bt shares cd-web's login
        cfg.username = "cd";
        cfg.password = config()->ripper_password;
        cfg.auth_type = HTTP_AUTH_TYPE_BASIC;
    }
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    if (!c) return -1;
    int status = -1, len = 0;
    if (post) {
        esp_http_client_set_header(c, "Content-Type", "application/json");
        esp_http_client_set_header(c, "X-Requested-With", "cd-web");  // the Pi's guard against cross-site posts
    }
    if (esp_http_client_open(c, post ? (int)strlen(post) : 0) == ESP_OK) {
        if (!post || esp_http_client_write(c, post, strlen(post)) == (int)strlen(post)) {
            esp_http_client_fetch_headers(c);
            status = esp_http_client_get_status_code(c);
            int n;
            while (len < BODY_MAX - 1 && (n = esp_http_client_read(c, body + len, BODY_MAX - 1 - len)) > 0) len += n;
        }
    }
    body[len] = 0;
    esp_http_client_cleanup(c);
    return status;
}

// The device's own text for a cd-bt message code; the Pi's (German) text only for codes this firmware does not know.
static void message_for(const cJSON *root, const char *text_key, char *out, size_t size)
{
    static const struct {
        const char *code;
        str_t text;
    } known[] = {
        {"connecting", S_CONNECTING_BOOMBOX},       {"speaker_unreachable", S_BB_SPEAKER_UNREACHABLE},
        {"no_speaker", S_BB_NO_SPEAKER},            {"playback_aborted", S_BB_ABORTED},
        {"album_not_found", S_BB_ALBUM_NOT_FOUND},  {"no_mp3s", S_BB_NO_MP3S},
        {"nothing_to_play", S_BB_NOTHING_TO_PLAY},  {"mpg123_missing", S_BB_PLAYER_MISSING},
    };
    const cJSON *code = cJSON_GetObjectItem(root, "code");
    if (cJSON_IsString(code)) {
        if (strcmp(code->valuestring, "album_end") == 0) {
            out[0] = 0;  // shown as "finished" by the player
            return;
        }
        for (size_t i = 0; i < sizeof(known) / sizeof(known[0]); i++) {
            if (strcmp(code->valuestring, known[i].code) == 0) {
                snprintf(out, size, "%s", tr(known[i].text));
                return;
            }
        }
    }
    copy_str(out, size, cJSON_GetObjectItem(root, text_key));
}

static boombox_state_t parse_state(const char *s)
{
    static const char *names[] = {"stopped", "connecting", "playing", "paused", "error"};
    for (int i = 0; i < 5; i++)
        if (strcmp(s, names[i]) == 0) return (boombox_state_t)i;
    return BB_ERROR;
}

static bool parse(int code, const char *body, boombox_status_t *s)
{
    cJSON *root = code > 0 ? cJSON_Parse(body) : NULL;
    if (!root) {
        s->state = BB_UNREACHABLE;
        snprintf(s->message, sizeof(s->message), "%s", tr(code < 0 ? S_RIPPER_UNREACHABLE : S_UNEXPECTED_ANSWER));
        return false;
    }
    bool ok = code == 200;
    if (ok) {
        char state[16];
        copy_str(state, sizeof(state), cJSON_GetObjectItem(root, "state"));
        s->state = parse_state(state);
        message_for(root, "message", s->message, sizeof(s->message));
        copy_str(s->album, sizeof(s->album), cJSON_GetObjectItem(root, "album"));
        copy_str(s->track, sizeof(s->track), cJSON_GetObjectItem(root, "track"));
        cJSON *pos = cJSON_GetObjectItem(root, "pos"), *dur = cJSON_GetObjectItem(root, "dur");
        s->pos = cJSON_IsNumber(pos) ? (float)pos->valuedouble : 0;
        s->dur = cJSON_IsNumber(dur) ? (float)dur->valuedouble : 0;
        cJSON *spk = cJSON_GetObjectItem(root, "speaker");
        s->speaker_connected = cJSON_IsTrue(cJSON_GetObjectItem(spk, "connected"));
        s->finished = cJSON_IsTrue(cJSON_GetObjectItem(root, "finished"));
    } else {
        message_for(root, "error", s->message, sizeof(s->message));
        if (!s->message[0]) snprintf(s->message, sizeof(s->message), tr(S_ERROR_N), code);
        ESP_LOGW(TAG, "%d: %s", code, s->message);
    }
    cJSON_Delete(root);
    return ok;
}

static bool post(const char *path, const char *json, boombox_status_t *s)
{
    char *body = malloc(BODY_MAX);
    if (!body) return false;
    bool ok = parse(request(path, json, body), body, s);
    free(body);
    return ok;
}

bool boombox_available(void) { return config()->boombox_url[0] != 0; }

bool boombox_status(boombox_status_t *s)
{
    char *body = malloc(BODY_MAX);
    if (!body) return false;
    int code = request("/api/bt/status", NULL, body);
    bool ok = parse(code, body, s);
    free(body);
    return ok || code > 0;
}

bool boombox_play(const char *album, const char *track, int pos_s, boombox_status_t *s)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "album", album);
    cJSON_AddStringToObject(o, "track", track);
    cJSON_AddNumberToObject(o, "pos", pos_s);
    char *json = cJSON_PrintUnformatted(o);
    cJSON_Delete(o);
    if (!json) return false;
    bool ok = post("/api/bt/play", json, s);
    cJSON_free(json);
    return ok;
}

bool boombox_toggle(boombox_status_t *s) { return post("/api/bt/toggle", "{}", s); }
bool boombox_stop(boombox_status_t *s) { return post("/api/bt/stop", "{}", s); }

bool boombox_seek(int delta_s, boombox_status_t *s)
{
    char json[32];
    snprintf(json, sizeof(json), "{\"delta\":%d}", delta_s);
    return post("/api/bt/seek", json, s);
}

bool boombox_step(int direction, boombox_status_t *s)
{
    char json[32];
    snprintf(json, sizeof(json), "{\"dir\":%d}", direction > 0 ? 1 : -1);
    return post("/api/bt/step", json, s);
}

bool boombox_volume(int percent, boombox_status_t *s)
{
    char json[32];
    snprintf(json, sizeof(json), "{\"volume\":%d}", percent);
    return post("/api/bt/volume", json, s);
}
