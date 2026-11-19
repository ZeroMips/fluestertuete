#include "player.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "board.h"
#include "boombox.h"
#include "i18n.h"
#include "driver/i2s_std.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "mp3s.h"
#include "ring.h"
#include "store.h"
#include "util.h"

static const char *TAG = "player";

#define RING_SIZE (1024 * 1024)  // about a minute of audio against Wi-Fi gaps
#define PREBUFFER (48 * 1024)
#define FETCH_CHUNK 4096
#define SAVE_INTERVAL_US (20 * 1000000LL)
#define RESTART_THRESHOLD_S 5  // "previous" within this time goes to the previous track
#define RESUME_REWIND_S 4      // pick the story up a little before where it stopped
#define DEFAULT_BPS 16000
#define DEFAULT_VOLUME 4
#define MIN_FRAMES_FOR_RATE 20
#define POLL_ACTIVE_US (1000 * 1000LL)  // boombox: how often to ask the Pi where it is
#define POLL_IDLE_US (5000 * 1000LL)

typedef enum { CMD_PLAY, CMD_TOGGLE, CMD_SEEK, CMD_STEP, CMD_JUMP, CMD_OUTPUT, CMD_VOLUME } cmd_type_t;

typedef struct {
    cmd_type_t type;
    int arg;
    uint32_t offset, bps;
    char *album;
    nas_entry_t *tracks;
} cmd_t;

// Everything below s_lock is shared between the tasks.
static SemaphoreHandle_t s_lock;
static ring_t s_ring;
static uint32_t s_gen;  // bumped per new read position; a fetch of an older generation stops
static bool s_eof, s_fatal, s_offline;
static char s_job_path[NAS_PATH_MAX];
static uint32_t s_job_offset, s_job_size;
static player_status_t s_status;
static nas_entry_t *s_tracks;
static int s_track_count;

// Owned by the audio task.
static QueueHandle_t s_cmds;
static i2s_chan_handle_t s_i2s;
static bool s_i2s_on;
static int s_rate = 44100;
static mp3s_t s_mp3;
static int16_t s_out[MP3S_MAX_SAMPLES * 2];
static char *s_album;
static int s_track;
static uint32_t s_start_offset, s_size, s_bps;
static bool s_bps_exact;
static uint64_t s_rate_bytes, s_rate_samples;
static int s_rate_frames;
static int64_t s_next_save;
static volatile int s_gain;
static volatile int s_volume;
// Boombox output, owned by the audio task: the Pi plays, this side mirrors where it is.
static bool s_remote;
static bool s_remote_ours;  // the Pi has our album loaded (playing or paused)
static float s_remote_pos;  // seconds into the track, as last reported
static int64_t s_next_poll;

#define LOCKED(...) do { xSemaphoreTake(s_lock, portMAX_DELAY); __VA_ARGS__; xSemaphoreGive(s_lock); } while (0)

static void set_state(player_state_t st) { LOCKED(s_status.state = st); }
static player_state_t get_state(void)
{
    player_state_t st;
    LOCKED(st = s_status.state);
    return st;
}

static bool get_finished(void)
{
    bool f;
    LOCKED(f = s_status.finished);
    return f;
}

static bool job_current(uint32_t gen)
{
    bool same;
    LOCKED(same = s_gen == gen);
    return same;
}

static void fetch_job(uint32_t gen, const char *path, uint32_t pos, uint32_t size)
{
    static uint8_t buf[FETCH_CHUNK];
    int api_errors = 0;
    while (pos < size && job_current(gen)) {
        nas_stream_t *st;
        nas_err_t err = nas_stream_open(path, pos, &st);
        if (err != NAS_OK) {
            ESP_LOGW(TAG, "open failed: %d", err);
            if (err == NAS_ERR_API && ++api_errors >= 3) {
                LOCKED(if (s_gen == gen) s_fatal = true);
                return;
            }
            LOCKED(s_offline = true);
            vTaskDelay(pdMS_TO_TICKS(2000));
            continue;
        }
        LOCKED(s_offline = false);
        api_errors = 0;
        while (pos < size) {
            int n = nas_stream_read(st, buf, sizeof(buf));
            if (n <= 0) break;  // dropped, for example after a long pause: reconnect at pos
            int done = 0;
            while (done < n) {
                bool same;
                size_t w = 0;
                LOCKED(same = s_gen == gen; if (same) w = ring_write(&s_ring, buf + done, n - done));
                if (!same) {
                    nas_stream_close(st);
                    return;
                }
                done += w;
                pos += w;
                if (done < n) vTaskDelay(pdMS_TO_TICKS(20));
            }
        }
        nas_stream_close(st);
    }
    LOCKED(if (s_gen == gen) s_eof = true);
}

static void fetch_task(void *arg)
{
    static char path[NAS_PATH_MAX];
    uint32_t done_gen = 0;
    for (;;) {
        uint32_t gen, offset, size;
        LOCKED(gen = s_gen; offset = s_job_offset; size = s_job_size; if (gen != done_gen) strlcpy(path, s_job_path, sizeof(path)));
        if (gen == done_gen) {
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        done_gen = gen;
        fetch_job(gen, path, offset, size);
    }
}

// A disabled channel keeps its DMA buffers, and enabling replays them from the first one: about 90 ms of
// whatever played before, heard as a scrap of the old album when a new one starts. Overwrite them with silence.
static void i2s_flush(void)
{
    static const uint8_t zeros[1024];
    size_t loaded;
    do {
        loaded = 0;
        if (i2s_channel_preload_data(s_i2s, zeros, sizeof(zeros), &loaded) != ESP_OK) break;
    } while (loaded == sizeof(zeros));
}

static void i2s_set(bool on)
{
    if (on == s_i2s_on) return;
    if (on) {
        ESP_ERROR_CHECK(i2s_channel_enable(s_i2s));
    } else {
        ESP_ERROR_CHECK(i2s_channel_disable(s_i2s));  // no clock lets the amplifier shut down: no hiss
        i2s_flush();
    }
    s_i2s_on = on;
}

static void i2s_set_rate(int rate)
{
    if (rate == s_rate || rate <= 0) return;
    bool was_on = s_i2s_on;
    i2s_set(false);
    i2s_std_clk_config_t clk = I2S_STD_CLK_DEFAULT_CONFIG(rate);
    ESP_ERROR_CHECK(i2s_channel_reconfig_std_clock(s_i2s, &clk));
    s_rate = rate;
    i2s_set(was_on);
}

static void i2s_setup(void)
{
    i2s_chan_config_t chan = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan.dma_desc_num = 8;
    chan.dma_frame_num = 512;
    chan.auto_clear = true;  // silence, not a repeating buffer, when data runs out
    ESP_ERROR_CHECK(i2s_new_channel(&chan, &s_i2s, NULL));
    i2s_std_config_t std = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(s_rate),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {.mclk = I2S_GPIO_UNUSED, .bclk = BOARD_I2S_BCLK, .ws = BOARD_I2S_WS, .dout = BOARD_I2S_DOUT, .din = I2S_GPIO_UNUSED},
    };
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(s_i2s, &std));
}

static uint32_t current_pos(void)
{
    if (s_remote) return (uint32_t)(s_remote_pos * s_bps);
    return s_start_offset + (uint32_t)s_mp3.consumed;
}

static void save_pos(void)
{
    if (!s_album || !s_tracks) return;
    store_pos_t p = {.track_hash = fnv1a(s_tracks[s_track].name), .offset = current_pos(), .bytes_per_sec = s_bps};
    store_set_pos(s_album, &p);
    s_next_save = esp_timer_get_time() + SAVE_INTERVAL_US;
}

static void publish_position(void)
{
    uint32_t pos = current_pos();
    LOCKED(s_status.pos_s = pos / s_bps; s_status.dur_s = s_size / s_bps);
}

static void load_track(int track, uint32_t offset)
{
    if (track != s_track) {
        s_rate_bytes = s_rate_samples = 0;
        s_rate_frames = 0;
        s_bps_exact = false;
    }
    s_track = track;
    s_size = s_tracks[track].size;
    s_start_offset = offset < s_size ? offset : 0;
    mp3s_reset(&s_mp3, s_start_offset == 0);
    i2s_set(false);  // nothing of the old position may sound: output resumes once the new data is buffered

    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_gen++;
    ring_reset(&s_ring);
    s_eof = s_fatal = false;
    snprintf(s_job_path, sizeof(s_job_path), "%s/%s", s_album, s_tracks[track].name);
    s_job_offset = s_start_offset;
    s_job_size = s_size;
    s_status.state = PLAYER_BUFFERING;
    s_status.finished = false;
    s_status.track = track;
    s_status.track_count = s_track_count;
    display_name(s_tracks[track].name, s_status.track_name, sizeof(s_status.track_name), true);
    xSemaphoreGive(s_lock);
    publish_position();
    ESP_LOGI(TAG, "track %d/%d at %lu", track + 1, s_track_count, (unsigned long)s_start_offset);
}

static void album_finished(void)
{
    i2s_set(false);
    store_clear_pos(s_album);
    LOCKED(s_status.state = PLAYER_STOPPED; s_status.finished = true; s_status.pos_s = 0);
}

static void track_finished(void)
{
    if (s_track + 1 < s_track_count) {
        load_track(s_track + 1, 0);
        save_pos();
    } else {
        album_finished();
    }
}

// ---- boombox output

static void set_track_fields(int track)
{
    s_track = track;
    s_size = s_tracks[track].size;
    LOCKED(s_status.track = track; s_status.track_count = s_track_count;
           display_name(s_tracks[track].name, s_status.track_name, sizeof(s_status.track_name), true));
}

// Linear: the boombox has its own volume knob, and the speaker's curve made the lower steps nearly silent there.
static int remote_volume(void) { return s_volume * 100 / PLAYER_VOLUME_MAX; }

// Mirrors the Pi's state into ours. ok: the command or status call succeeded.
static void remote_apply(const boombox_status_t *b, bool ok)
{
    bool ours = s_album && b->album[0] && strcmp(b->album, s_album) == 0;
    if (ours && b->track[0] && (s_track < 0 || strcmp(s_tracks[s_track].name, b->track) != 0)) {
        for (int i = 0; i < s_track_count; i++)
            if (strcmp(s_tracks[i].name, b->track) == 0) set_track_fields(i);
    }
    if (ours && b->dur > 1 && s_size) s_bps = (uint32_t)(s_size / b->dur);
    if (ours) s_remote_pos = b->pos;
    s_remote_ours = ours && (b->state == BB_PLAYING || b->state == BB_PAUSED || b->state == BB_CONNECTING);

    player_state_t st;
    bool finished = false;
    switch (b->state) {
    case BB_PLAYING: st = PLAYER_PLAYING; break;
    case BB_PAUSED: st = PLAYER_PAUSED; break;
    case BB_CONNECTING: st = PLAYER_BUFFERING; break;
    case BB_STOPPED:
        st = PLAYER_STOPPED;
        finished = ours && b->finished;
        break;
    default: st = PLAYER_ERROR; break;
    }
    if (!ok && b->state != BB_UNREACHABLE) st = PLAYER_ERROR;  // a refused command: show why
    if (!ours && ok && st != PLAYER_ERROR) st = get_state() == PLAYER_STOPPED ? PLAYER_STOPPED : PLAYER_PAUSED;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_offline = b->state == BB_UNREACHABLE;
    s_status.state = st;
    s_status.finished = finished;
    strlcpy(s_status.message, finished ? "" : b->message, sizeof(s_status.message));
    if (ours) {
        s_status.pos_s = finished ? 0 : (int)b->pos;
        s_status.dur_s = (int)b->dur;
    }
    xSemaphoreGive(s_lock);
    if (finished && s_album) store_clear_pos(s_album);
}

static void remote_poll(void)
{
    boombox_status_t b = {0};
    bool ok = boombox_status(&b);
    remote_apply(&b, ok);
    player_state_t st = get_state();
    bool active = st == PLAYER_PLAYING || st == PLAYER_BUFFERING;
    s_next_poll = esp_timer_get_time() + (active ? POLL_ACTIVE_US : POLL_IDLE_US);
    if (st == PLAYER_PLAYING && esp_timer_get_time() >= s_next_save) save_pos();
}

static void remote_start(int track, uint32_t offset)
{
    set_track_fields(track);
    s_remote_pos = (float)offset / s_bps;
    LOCKED(s_status.state = PLAYER_BUFFERING; s_status.finished = false; s_status.pos_s = (int)s_remote_pos;
           s_status.dur_s = s_size / s_bps; strlcpy(s_status.message, tr(S_CONNECTING_BOOMBOX), sizeof(s_status.message)));
    boombox_status_t b = {0};
    if (boombox_volume(remote_volume(), &b) || b.state != BB_UNREACHABLE) {
        bool ok = boombox_play(s_album, s_tracks[track].name, (int)s_remote_pos, &b);
        remote_apply(&b, ok);
    } else {
        remote_apply(&b, false);
    }
    s_next_poll = esp_timer_get_time() + POLL_ACTIVE_US;
    ESP_LOGI(TAG, "boombox: track %d/%d at %d s", track + 1, s_track_count, (int)s_remote_pos);
}

static void remote_cmd(cmd_t *c)
{
    boombox_status_t b = {0};
    bool ok = true;
    player_state_t st = get_state();
    switch (c->type) {
    case CMD_TOGGLE:
        if (s_remote_ours && (st == PLAYER_PLAYING || st == PLAYER_PAUSED || st == PLAYER_BUFFERING)) {
            ok = boombox_toggle(&b);
            remote_apply(&b, ok);
            save_pos();
        } else {
            remote_start(s_track >= 0 ? s_track : 0, st == PLAYER_STOPPED && get_finished() ? 0 : current_pos());
        }
        return;
    case CMD_SEEK: ok = boombox_seek(c->arg, &b); break;
    case CMD_STEP: ok = boombox_step(c->arg, &b); break;
    case CMD_JUMP:
        if (c->arg >= 0 && c->arg < s_track_count) remote_start(c->arg, 0);
        return;
    case CMD_VOLUME: ok = boombox_volume(remote_volume(), &b); break;
    default: return;
    }
    remote_apply(&b, ok);
    save_pos();
}

static void handle_play(cmd_t *c)
{
    if (s_album) save_pos();
    free(s_album);
    s_album = c->album;
    nas_entry_t *old = s_tracks;
    int old_count = s_track_count;
    LOCKED(s_tracks = c->tracks; s_track_count = c->arg >> 16; strlcpy(s_status.album, s_album, sizeof(s_status.album)));
    nas_free_entries(old, old_count);
    s_bps = c->bps ? c->bps : DEFAULT_BPS;
    uint32_t rewind = RESUME_REWIND_S * s_bps;
    s_track = -1;
    uint32_t start = c->offset > rewind ? c->offset - rewind : 0;
    if (s_remote) {
        remote_start(c->arg & 0xFFFF, start);
        return;
    }
    load_track(c->arg & 0xFFFF, start);
    s_bps_exact = c->bps != 0;  // a saved rate spares a mid-file resume from guessing
}

// Moves what plays to the other output, at the same place; paused stays paused.
static void set_output(bool remote)
{
    if (remote == s_remote) return;
    player_state_t st = get_state();
    bool running = st == PLAYER_PLAYING || st == PLAYER_BUFFERING;
    bool loaded = s_album && s_tracks && s_track >= 0;
    if (s_remote) {  // leaving the boombox: take its position along
        boombox_status_t b = {0};
        remote_apply(&b, boombox_status(&b));
        if (s_remote_ours) {
            running = get_state() == PLAYER_PLAYING || get_state() == PLAYER_BUFFERING;
            boombox_stop(&b);
        }
    } else {
        i2s_set(false);
        LOCKED(s_gen++; ring_reset(&s_ring));  // stops the download
    }
    uint32_t pos = loaded ? current_pos() : 0;
    s_remote = remote;
    s_remote_ours = false;
    store_set_boombox(remote);
    LOCKED(s_status.boombox = remote; s_status.message[0] = 0; s_offline = false);
    ESP_LOGI(TAG, "output: %s", remote ? "boombox" : "speaker");
    if (!loaded) {
        if (remote) s_next_poll = 0;
        return;
    }
    bool stopped = get_state() == PLAYER_STOPPED;  // e.g. the album has ended: stays so
    if (remote) {
        if (running) remote_start(s_track, pos);
        else s_remote_pos = (float)pos / s_bps;
        if (!running && !stopped) set_state(PLAYER_PAUSED);
    } else if (!stopped || running) {
        load_track(s_track, pos);
        if (!running) set_state(PLAYER_PAUSED);
    }
    if (!stopped) save_pos();
}

static void handle_seek(int seconds)
{
    int64_t target = (int64_t)current_pos() + (int64_t)seconds * s_bps;
    if (target >= s_size) {
        track_finished();
        return;
    }
    load_track(s_track, target < 0 ? 0 : (uint32_t)target);
    save_pos();
}

static void handle_cmd(cmd_t *c)
{
    player_state_t st = get_state();
    if (c->type == CMD_PLAY) {
        handle_play(c);
        return;
    }
    if (c->type == CMD_OUTPUT) {
        set_output(c->arg != 0);
        return;
    }
    if (s_remote) {
        if (s_tracks || c->type == CMD_VOLUME) remote_cmd(c);
        return;
    }
    if (!s_tracks) return;
    switch (c->type) {
    case CMD_TOGGLE:
        if (st == PLAYER_PLAYING || st == PLAYER_BUFFERING) {
            i2s_set(false);
            save_pos();
            set_state(PLAYER_PAUSED);
        } else if (st == PLAYER_PAUSED) {
            set_state(PLAYER_BUFFERING);
        } else if (st == PLAYER_ERROR) {
            load_track(s_track, current_pos());
        } else {
            load_track(0, 0);
        }
        break;
    case CMD_SEEK:
        handle_seek(c->arg);
        break;
    case CMD_STEP:
        if (c->arg > 0) {
            if (s_track + 1 < s_track_count) load_track(s_track + 1, 0);
        } else if (current_pos() / s_bps > RESTART_THRESHOLD_S || s_track == 0) {
            load_track(s_track, 0);
        } else {
            load_track(s_track - 1, 0);
        }
        save_pos();
        break;
    case CMD_JUMP:
        if (c->arg >= 0 && c->arg < s_track_count) {
            load_track(c->arg, 0);
            save_pos();
        }
        break;
    default:
        break;
    }
}

static void output_frame(const mp3s_frame_t *f)
{
    i2s_set_rate(f->sample_rate);
    int gain = s_gain;
    int frames = f->samples / f->channels;
    // One speaker: mix down and send the same sample to both I2S slots.
    for (int i = 0; i < frames; i++) {
        int m = f->channels == 2 ? (s_mp3.pcm[2 * i] + s_mp3.pcm[2 * i + 1]) / 2 : s_mp3.pcm[i];
        int16_t v = (int16_t)((m * gain) >> 15);
        s_out[2 * i] = v;
        s_out[2 * i + 1] = v;
    }
    size_t written;
    i2s_channel_write(s_i2s, s_out, frames * 4, &written, 1000);

    s_rate_bytes += f->bytes;
    s_rate_samples += frames;
    s_rate_frames++;
    if (s_mp3.file_frames && !s_bps_exact) {
        s_bps = (uint32_t)((uint64_t)s_size * f->sample_rate / ((uint64_t)s_mp3.file_frames * frames));
        s_bps_exact = true;
    } else if (!s_bps_exact && s_rate_frames >= MIN_FRAMES_FOR_RATE) {
        s_bps = (uint32_t)(s_rate_bytes * f->sample_rate / s_rate_samples);
    }
    if (!s_bps) s_bps = DEFAULT_BPS;
    if ((s_rate_frames & 15) == 0) publish_position();
}

static void pump(void)
{
    uint8_t *dst;
    size_t space = mp3s_space(&s_mp3, &dst), got = 0, used;
    bool eof, fatal, fetch_done;
    LOCKED(if (s_mp3.len < MP3S_IN_SIZE / 2) got = ring_read(&s_ring, dst, space);
           used = ring_used(&s_ring); fetch_done = s_eof; eof = s_eof && used == 0; fatal = s_fatal);
    mp3s_commit(&s_mp3, got);

    if (get_state() == PLAYER_BUFFERING) {
        if (used < PREBUFFER && !fetch_done && !fatal) {
            vTaskDelay(pdMS_TO_TICKS(10));
            return;
        }
        set_state(PLAYER_PLAYING);
        i2s_set(true);
    }

    mp3s_frame_t f;
    switch (mp3s_next(&s_mp3, eof, &f)) {
    case MP3S_FRAME:
        output_frame(&f);
        if (esp_timer_get_time() >= s_next_save) save_pos();
        break;
    case MP3S_NEED_DATA:
        if (fatal && used == 0) {
            i2s_set(false);
            set_state(PLAYER_ERROR);
        } else if (used == 0 && !fetch_done) {
            set_state(PLAYER_BUFFERING);
        }
        vTaskDelay(pdMS_TO_TICKS(2));
        break;
    case MP3S_END:
        track_finished();
        break;
    }
}

static void audio_task(void *arg)
{
    for (;;) {
        cmd_t c;
        player_state_t st = get_state();
        bool active = st == PLAYER_PLAYING || st == PLAYER_BUFFERING;
        bool local = !s_remote;
        if (xQueueReceive(s_cmds, &c, local && active ? 0 : pdMS_TO_TICKS(100)) == pdTRUE) {
            handle_cmd(&c);
            continue;
        }
        if (!local) {
            if (s_album && esp_timer_get_time() >= s_next_poll) remote_poll();
        } else if (active) {
            pump();
        }
    }
}

static void send(cmd_t c) { xQueueSend(s_cmds, &c, portMAX_DELAY); }

void player_play(const char *album, nas_entry_t *tracks, int count, int track, uint32_t offset, uint32_t bps)
{
    send((cmd_t){.type = CMD_PLAY, .arg = count << 16 | track, .offset = offset, .bps = bps, .album = strdup(album), .tracks = tracks});
}
void player_toggle(void) { send((cmd_t){.type = CMD_TOGGLE}); }
void player_seek(int seconds) { send((cmd_t){.type = CMD_SEEK, .arg = seconds}); }
void player_step(int direction) { send((cmd_t){.type = CMD_STEP, .arg = direction}); }
void player_jump(int track) { send((cmd_t){.type = CMD_JUMP, .arg = track}); }

void player_get_status(player_status_t *s)
{
    LOCKED(*s = s_status; s->offline = s_offline);
}

bool player_copy_tracks(nas_entry_t **tracks, int *count)
{
    nas_entry_t *copy = NULL;
    int n = 0;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_track_count && (copy = calloc(s_track_count, sizeof(*copy)))) {
        n = s_track_count;
        for (int i = 0; i < n; i++) {
            copy[i] = s_tracks[i];
            copy[i].name = strdup(s_tracks[i].name);
        }
    }
    xSemaphoreGive(s_lock);
    *tracks = copy;
    *count = n;
    return copy != NULL;
}

void player_set_boombox(bool on) { send((cmd_t){.type = CMD_OUTPUT, .arg = on}); }

int player_volume(void) { return s_volume; }

void player_set_volume(int volume)
{
    if (volume < 0) volume = 0;
    if (volume > PLAYER_VOLUME_MAX) volume = PLAYER_VOLUME_MAX;
    s_volume = volume;
    s_gain = volume * volume * 32767 / (PLAYER_VOLUME_MAX * PLAYER_VOLUME_MAX);  // loudness is not linear
    store_set_volume(volume);
    if (s_cmds && s_status.boombox) send((cmd_t){.type = CMD_VOLUME});  // the Pi has its own volume
}

void player_init(void)
{
    s_lock = xSemaphoreCreateMutex();
    s_cmds = xQueueCreate(8, sizeof(cmd_t));
    uint8_t *mem = heap_caps_malloc(RING_SIZE, MALLOC_CAP_SPIRAM);
    assert(mem);
    ring_init(&s_ring, mem, RING_SIZE);
    assert(mp3s_init(&s_mp3));
    s_bps = DEFAULT_BPS;
    s_remote = s_status.boombox = store_get_boombox() && boombox_available();
    player_set_volume(store_get_volume(DEFAULT_VOLUME));
    i2s_setup();
#if CONFIG_SPIRAM_ALLOW_STACK_EXTERNAL_MEMORY
    xTaskCreatePinnedToCoreWithCaps(audio_task, "audio", 8192, NULL, 12, NULL, 1, MALLOC_CAP_SPIRAM);  // positions via flash_run()
#else
    xTaskCreatePinnedToCore(audio_task, "audio", 8192, NULL, 12, NULL, 1);
#endif
    // The download bursts at full Wi-Fi speed when a track starts (TLS handshake, decryption, a megabyte of copying).
    // On the UI's core that froze the screen for over a second, so it shares the audio core, below the decoder.
#if CONFIG_SPIRAM_ALLOW_STACK_EXTERNAL_MEMORY
    // Network only, never flash: its stack may live in PSRAM, where the Core2 has room and internal RAM has not.
    xTaskCreatePinnedToCoreWithCaps(fetch_task, "fetch", 10240, NULL, 5, NULL, 1, MALLOC_CAP_SPIRAM);
#else
    xTaskCreatePinnedToCore(fetch_task, "fetch", 10240, NULL, 5, NULL, 1);
#endif
}
