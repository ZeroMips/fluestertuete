#include "ripper.h"

#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "index.h"
#include "config.h"
#include "i18n.h"
#include "util.h"


#define BODY_MAX (32 * 1024)
#define WATCH_MS 2000
#define IDLE_MS 20000

static const char *TAG = "ripper";
static const char *CLS_KEYS[RIP_CLS_COUNT] = {"KIDS", "SPOKEN", "MUSIC", "UNSORTED"};

typedef enum { ACT_NONE, ACT_CORRECT, ACT_FORCE, ACT_EJECT } action_t;

static SemaphoreHandle_t s_lock;
static TaskHandle_t s_task;
static ripper_status_t s_st;
static volatile bool s_watching;
static action_t s_action;
static int s_action_arg;
static char s_release[48];  // a release correction made on the web page, kept when the T-Deck changes the type
static char s_reply[96];
static bool s_has_reply;
static char *s_body;

const char *ripper_state_name(rip_state_t s)
{
    switch (s) {
    case RIP_IDLE: return tr(S_RIP_IDLE);
    case RIP_READING: return tr(S_RIP_READING);
    case RIP_RIPPING: return tr(S_RIP_RIPPING);
    case RIP_DELIVERING: return tr(S_RIP_DELIVERING);
    case RIP_DONE: return tr(S_RIP_DONE);
    case RIP_ERROR: return tr(S_RIP_ERROR);
    default: return tr(S_RIP_UNKNOWN);
    }
}

const char *ripper_cls_name(rip_cls_t c)
{
    static const str_t names[RIP_CLS_COUNT] = {S_CLS_KIDS, S_CLS_SPOKEN, S_CLS_MUSIC, S_CLS_UNSORTED};
    return c < RIP_CLS_COUNT ? tr(names[c]) : "?";
}

static rip_state_t parse_state(const char *s)
{
    static const char *names[] = {"idle", "reading", "ripping", "delivering", "done", "error"};
    for (int i = 0; s && i < 6; i++) {
        if (strcmp(s, names[i]) == 0) return (rip_state_t)i;
    }
    return RIP_UNKNOWN;
}

static int cls_from_key(const cJSON *item)
{
    for (int i = 0; cJSON_IsString(item) && i < RIP_CLS_COUNT; i++) {
        if (strcmp(item->valuestring, CLS_KEYS[i]) == 0) return i;
    }
    return RIP_CLS_NONE;
}

static void copy_str(char *out, size_t size, const cJSON *item)
{
    strlcpy(out, cJSON_IsString(item) ? item->valuestring : "", size);
}

// One request; the answer lands in s_body. Returns the HTTP status, or -1 when the ripper is unreachable.
static int request(const char *path, const char *post)
{
    char url[192];
    snprintf(url, sizeof(url), "%s%s", config()->ripper_url, path);
    esp_http_client_config_t cfg = {
        .url = url,
        .method = post ? HTTP_METHOD_POST : HTTP_METHOD_GET,
        .timeout_ms = post ? 30000 : 5000,  // eject waits for the drive
        .disable_auto_redirect = true,
    };
    if (config()->ripper_password[0]) {
        cfg.username = "cd";
        cfg.password = config()->ripper_password;
        cfg.auth_type = HTTP_AUTH_TYPE_BASIC;
    }
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    if (!c) return -1;
    int status = -1, len = 0;
    if (post) {
        esp_http_client_set_header(c, "Content-Type", "application/json");
        esp_http_client_set_header(c, "X-Requested-With", "cd-web");  // cd-web's guard against cross-site posts
    }
    if (esp_http_client_open(c, post ? (int)strlen(post) : 0) == ESP_OK) {
        if (!post || esp_http_client_write(c, post, strlen(post)) == (int)strlen(post)) {
            esp_http_client_fetch_headers(c);
            status = esp_http_client_get_status_code(c);
            int n;
            while (len < BODY_MAX - 1 && (n = esp_http_client_read(c, s_body + len, BODY_MAX - 1 - len)) > 0) len += n;
        }
    }
    s_body[len] = 0;
    esp_http_client_cleanup(c);
    return status;
}

static void poll_status(void)
{
    static uint32_t last_hash;
    static bool have_hash, refresh_pending;
    int status = request("/api/status", NULL);
    cJSON *root = status == 200 ? cJSON_Parse(s_body) : NULL;
    if (!root) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        if (s_st.online) ESP_LOGW(TAG, "not reachable (%d)", status);
        s_st.online = false;
        xSemaphoreGive(s_lock);
        return;
    }
    ripper_status_t st;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    st = s_st;
    xSemaphoreGive(s_lock);

    const cJSON *s = cJSON_GetObjectItem(root, "status");
    const cJSON *state = cJSON_GetObjectItem(s, "state");
    st.online = st.seen = true;
    st.state = parse_state(cJSON_IsString(state) ? state->valuestring : NULL);
    const cJSON *prog = cJSON_GetObjectItem(s, "progress");
    st.progress = (st.state == RIP_RIPPING || st.state == RIP_DELIVERING) && cJSON_IsNumber(prog)
                      ? (int)(prog->valuedouble * 100 + 0.5)
                      : -1;
    copy_str(st.artist, sizeof(st.artist), cJSON_GetObjectItem(s, "artist"));
    copy_str(st.album, sizeof(st.album), cJSON_GetObjectItem(s, "album"));
    copy_str(st.cls, sizeof(st.cls), cJSON_GetObjectItem(s, "cls"));
    copy_str(st.message, sizeof(st.message), cJSON_GetObjectItem(s, "message"));

    // The status names the type by its label; "classes" maps keys to labels.
    st.detected = RIP_CLS_NONE;
    const cJSON *classes = cJSON_GetObjectItem(root, "classes");
    for (int i = 0; i < RIP_CLS_COUNT; i++) {
        const cJSON *label = cJSON_GetObjectItem(classes, CLS_KEYS[i]);
        if (cJSON_IsString(label) && strcmp(label->valuestring, st.cls) == 0) st.detected = i;
    }
    st.choices = 0;
    const cJSON *c;
    cJSON_ArrayForEach(c, cJSON_GetObjectItem(cJSON_GetObjectItem(s, "detect"), "choices"))
    {
        int k = cls_from_key(c);
        if (k != RIP_CLS_NONE) st.choices |= 1u << k;
    }
    const cJSON *corr = cJSON_GetObjectItem(root, "correction");
    int corrected = cls_from_key(cJSON_GetObjectItem(corr, "cls"));
    st.chosen = corrected != RIP_CLS_NONE ? corrected : st.detected;
    const cJSON *rel = cJSON_GetObjectItem(corr, "release");
    st.release_pending = cJSON_IsString(rel) && rel->valuestring[0];
    char release[sizeof(s_release)];
    strlcpy(release, st.release_pending ? rel->valuestring : "", sizeof(release));
    st.force = cJSON_IsTrue(cJSON_GetObjectItem(root, "force"));
    st.already = st.state == RIP_DONE && (cJSON_IsString(cJSON_GetObjectItem(s, "library")) ||
                                          strncmp(st.message, "Schon ", 6) == 0);
    st.waiting = st.failed = 0;
    const cJSON *job;
    cJSON_ArrayForEach(job, cJSON_GetObjectItem(root, "jobs"))
    {
        if (cJSON_IsTrue(cJSON_GetObjectItem(job, "failed"))) st.failed++;
        else st.waiting++;
    }

    // The newest finished album; a change means a rip just went onto the NAS.
    const cJSON *h = cJSON_GetArrayItem(cJSON_GetObjectItem(root, "history"), 0);
    if (h) {
        char artist[RIPPER_TEXT_MAX], album[RIPPER_TEXT_MAX], cls[32], time[32];
        copy_str(artist, sizeof(artist), cJSON_GetObjectItem(h, "artist"));
        copy_str(album, sizeof(album), cJSON_GetObjectItem(h, "album"));
        copy_str(cls, sizeof(cls), cJSON_GetObjectItem(h, "cls"));
        copy_str(time, sizeof(time), cJSON_GetObjectItem(h, "time"));
        snprintf(st.last, sizeof(st.last), "%s \xE2\x80\x93 %s", artist, album);
        uint32_t hash = fnv1a(time) ^ fnv1a(album);
        if (have_hash && hash != last_hash) {
            st.finished++;
            st.last_kids = strcmp(cls, "Kinderh\xC3\xB6rspiel") == 0;
            ESP_LOGI(TAG, "finished: %s (%s)", st.last, cls);
            if (st.last_kids) refresh_pending = true;
        }
        last_hash = hash;
        have_hash = true;
    }
    cJSON_Delete(root);

    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_st = st;
    strlcpy(s_release, release, sizeof(s_release));
    xSemaphoreGive(s_lock);

    // A new Hörspiel is on the share: crawl so it shows up in the list. A running crawl may have missed
    // it, so wait for that to end and ask again.
    if (refresh_pending && index_progress() < 0) {
        refresh_pending = false;
        index_request_refresh();
    }
}

static void set_reply(const char *text)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    strlcpy(s_reply, text, sizeof(s_reply));
    s_has_reply = true;
    xSemaphoreGive(s_lock);
}

static void run_action(action_t a, int arg)
{
    char body[128], text[96];
    const char *path;
    ripper_status_t st;
    char release[sizeof(s_release)];
    xSemaphoreTake(s_lock, portMAX_DELAY);
    st = s_st;
    strlcpy(release, s_release, sizeof(release));
    xSemaphoreGive(s_lock);
    if (a == ACT_CORRECT) {
        // The detected type means "no correction" (null). A release chosen on the web page is sent back
        // unchanged, else cd-web would drop it.
        path = "/api/correct";
        if (arg == st.detected) snprintf(body, sizeof(body), "{\"cls\":null,\"release\":\"%s\"}", release);
        else snprintf(body, sizeof(body), "{\"cls\":\"%s\",\"release\":\"%s\"}", CLS_KEYS[arg], release);
    } else if (a == ACT_FORCE) {
        path = "/api/force";
        snprintf(body, sizeof(body), "{\"on\":%s}", arg ? "true" : "false");
    } else {
        path = "/api/eject";
        snprintf(body, sizeof(body), "{}");
    }
    int status = request(path, body);
    if (status < 0) {
        set_reply(tr(S_RIPPER_UNREACHABLE));
        return;
    }
    if (status == 200) {
        if (a == ACT_CORRECT) snprintf(text, sizeof(text), tr(S_TYPE_SET), ripper_cls_name((rip_cls_t)arg));
        else if (a == ACT_FORCE) snprintf(text, sizeof(text), "%s", tr(arg ? S_NEXT_CD_RIPPED : S_CANCELLED));
        else snprintf(text, sizeof(text), "%s", tr(S_EJECTED));
    } else {
        cJSON *root = cJSON_Parse(s_body);
        const cJSON *err = cJSON_GetObjectItem(root, "error");
        if (cJSON_IsString(err)) strlcpy(text, err->valuestring, sizeof(text));
        else snprintf(text, sizeof(text), tr(S_ERROR_N), status);
        cJSON_Delete(root);
    }
    ESP_LOGI(TAG, "%s -> %d %s", path, status, text);
    set_reply(text);
}

static void task(void *arg)
{
    for (;;) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        action_t a = s_action;
        int arg = s_action_arg;
        s_action = ACT_NONE;
        xSemaphoreGive(s_lock);
        if (a != ACT_NONE) run_action(a, arg);
        poll_status();
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(s_watching ? WATCH_MS : IDLE_MS));
    }
}

bool ripper_available(void) { return config()->ripper_url[0] != 0; }

void ripper_start(void)
{
    s_body = heap_caps_malloc(BODY_MAX, MALLOC_CAP_SPIRAM);
    s_lock = xSemaphoreCreateMutex();
    if (!s_body || !s_lock) return;
    s_st.state = RIP_UNKNOWN;
    s_st.progress = -1;
    s_st.detected = s_st.chosen = RIP_CLS_NONE;
    if (!ripper_available()) return;  // no ripper set up: status stays "unknown", nothing polls
#if CONFIG_SPIRAM_ALLOW_STACK_EXTERNAL_MEMORY
    xTaskCreatePinnedToCoreWithCaps(task, "ripper", 6144, NULL, 2, &s_task, 0, MALLOC_CAP_SPIRAM);  // network only
#else
    xTaskCreatePinnedToCore(task, "ripper", 6144, NULL, 2, &s_task, 0);
#endif
}

void ripper_set_watching(bool on)
{
    bool was = s_watching;
    s_watching = on;
    if (on && !was && s_task) xTaskNotifyGive(s_task);  // fresh numbers as the screen opens
}

void ripper_get(ripper_status_t *out)
{
    if (!s_lock) {
        memset(out, 0, sizeof(*out));
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    *out = s_st;
    xSemaphoreGive(s_lock);
}

static void post_action(action_t a, int arg)
{
    if (!s_lock || !s_task) return;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_action = a;
    s_action_arg = arg;
    xSemaphoreGive(s_lock);
    xTaskNotifyGive(s_task);
}

void ripper_correct(rip_cls_t cls) { post_action(ACT_CORRECT, cls); }
void ripper_set_force(bool on) { post_action(ACT_FORCE, on); }
void ripper_eject(void) { post_action(ACT_EJECT, 0); }

bool ripper_take_reply(char *text, int size)
{
    if (!s_lock) return false;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool has = s_has_reply;
    if (has) strlcpy(text, s_reply, size);
    s_has_reply = false;
    xSemaphoreGive(s_lock);
    return has;
}
