#include "ui.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "board.h"
#include "boombox.h"
#include "i18n.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "index.h"
#include "nas.h"
#include "player.h"
#include "ripper.h"
#include "store.h"
#include "util.h"

static const char *TAG = "ui";

LV_FONT_DECLARE(font_roboto_16);
LV_FONT_DECLARE(font_roboto_20);

#define COLOR_BG lv_color_hex(0x10131A)
#define COLOR_ROW lv_color_hex(0x232836)
#define COLOR_SEL lv_color_hex(0x2F6FDE)
#define COLOR_ACCENT lv_color_hex(0xFFB020)
#define COLOR_DIM lv_color_hex(0x9AA3B2)
#define COLOR_TEXT lv_color_hex(0xFFFFFF)
#define COLOR_BRAND lv_color_hex(0xF08A3E)  // the logo's orange, as on the website

extern const lv_image_dsc_t logo_img;  // logo_img.c, from web/logo.svg
static void one_line(lv_obj_t *l);

#define HEADER_H 36
#define ROW_H 46
#define ROW_H_TWO_LINES 52
#define SEARCH_MAX_HITS 30
#define SEARCH_QUERY_MAX 32
#define SEARCH_DELAY_MS 150
#define SEEK_STEP_S 30
#define SCREEN_OFF_US (60 * 1000000LL)
#define BATTERY_POLL_US (30 * 1000000LL)

typedef enum { V_BOOT, V_BROWSE, V_TRACKS, V_PLAYER, V_SEARCH, V_RIPPER } view_t;
typedef enum { ROW_DIR, ROW_PLAY_ALL, ROW_RESUME, ROW_TRACK, ROW_HIT, ROW_INFO, ROW_RESYNC } row_kind_t;
typedef enum { A_BACK, A_TRACKS, A_PREV, A_REWIND, A_TOGGLE, A_FORWARD, A_NEXT, A_VOL_DOWN, A_VOL_UP, A_PLAYER, A_RIPPER, A_OUTPUT } action_t;
typedef enum { NAV_OPEN, NAV_PLAY } nav_type_t;

typedef struct {
    row_kind_t kind;
    char *name;  // folder name; full path for ROW_RESUME and ROW_HIT
    int track;   // ROW_TRACK: index; ROW_HIT: search kind
    lv_obj_t *obj;
    lv_obj_t *label;
} row_t;

typedef struct {
    lv_obj_t *root, *back, *ripper, *title, *battery, *player, *list;
    row_t *rows;
    int count, sel;
} list_view_t;

typedef struct {
    nav_type_t type;
    char path[NAS_PATH_MAX];
    char focus[256];
} nav_msg_t;

static view_t s_view = V_BOOT;
static lv_obj_t *s_boot, *s_boot_label, *s_toast;
static list_view_t s_browse, s_tracks, s_search;
static view_t s_search_return;
static char s_query[SEARCH_QUERY_MAX + 1];
static lv_timer_t *s_search_timer;
static bool s_resync_asked;  // the next finished crawl answers a tap on "neu einlesen"
static char s_cur_path[NAS_PATH_MAX];
static QueueHandle_t s_nav;

static struct {
    lv_obj_t *root, *album, *title, *info, *bar, *pos, *dur, *toggle, *vol, *battery, *output;
    bool boombox;  // last shown output, so a switch can be announced
} s_pl;

// Focus indices on the ripper screen: the type buttons, then these.
#define RIP_EJECT RIP_CLS_COUNT
#define RIP_FORCE (RIP_CLS_COUNT + 1)

static struct {
    lv_obj_t *root, *battery, *state, *title, *sub, *bar, *type, *last_head, *last;
    lv_obj_t *buttons[RIP_FORCE + 1];
    int focus;
    ripper_status_t st;  // as last shown
} s_rip;

static int64_t s_last_input;
static bool s_screen_on = true, s_touch_swallow;

static void add_row(list_view_t *v, row_t *r, int index, const char *text, const char *sub);

static void list_clear(list_view_t *v)
{
    lv_obj_clean(v->list);
    for (int i = 0; i < v->count; i++) free(v->rows[i].name);
    free(v->rows);
    v->rows = NULL;
    v->count = 0;
    v->sel = -1;
}

static void list_select_anim(list_view_t *v, int i, lv_anim_enable_t anim)
{
    if (i < 0 || i >= v->count) return;
    if (v->sel >= 0 && v->sel < v->count) {
        lv_obj_remove_state(v->rows[v->sel].obj, LV_STATE_CHECKED);
        lv_label_set_long_mode(v->rows[v->sel].label, LV_LABEL_LONG_DOT);
    }
    v->sel = i;
    lv_obj_add_state(v->rows[i].obj, LV_STATE_CHECKED);
    lv_label_set_long_mode(v->rows[i].label, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_scroll_to_view(v->rows[i].obj, anim);
}

// Trackball steps glide; a freshly built list shows its selection at once.
static void list_select(list_view_t *v, int i) { list_select_anim(v, i, LV_ANIM_ON); }

static void show_view(view_t view)
{
    s_view = view;
    lv_obj_add_flag(s_boot, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_browse.root, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_tracks.root, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_pl.root, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_search.root, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_rip.root, LV_OBJ_FLAG_HIDDEN);
    lv_obj_t *o = view == V_BOOT     ? s_boot
                  : view == V_BROWSE ? s_browse.root
                  : view == V_TRACKS ? s_tracks.root
                  : view == V_SEARCH ? s_search.root
                  : view == V_RIPPER ? s_rip.root
                                     : s_pl.root;
    lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
    ripper_set_watching(view == V_RIPPER);
}

static void toast_hide(lv_timer_t *t) { lv_obj_add_flag(s_toast, LV_OBJ_FLAG_HIDDEN); }

// A symbol, a space, then a translated format: LV_SYMBOL_PLAY " Weiterhören: …".
static void symbol_text(char *out, size_t size, const char *symbol, const char *fmt, ...)
{
    int n = snprintf(out, size, "%s ", symbol);
    if (n < 0 || (size_t)n >= size) return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(out + n, size - n, fmt, ap);
    va_end(ap);
}

static void toast(const char *text)
{
    lv_label_set_text(s_toast, text);
    lv_obj_remove_flag(s_toast, LV_OBJ_FLAG_HIDDEN);
    lv_timer_t *t = lv_timer_create(toast_hide, 3000, NULL);
    lv_timer_set_repeat_count(t, 1);
}

static void nav_post(nav_type_t type, const char *path, const char *focus)
{
    nav_msg_t *m = calloc(1, sizeof(*m));
    if (!m) return;
    m->type = type;
    strlcpy(m->path, path, sizeof(m->path));
    if (focus) strlcpy(m->focus, focus, sizeof(m->focus));
    if (xQueueSend(s_nav, &m, 0) != pdTRUE) free(m);
}

static void go_up(void)
{
    if (strcmp(s_cur_path, NAS_ROOT) == 0) return;
    const char *slash = strrchr(s_cur_path, '/');
    if (!slash) return;
    char parent[NAS_PATH_MAX];
    snprintf(parent, sizeof(parent), "%.*s", (int)(slash - s_cur_path), s_cur_path);
    nav_post(NAV_OPEN, parent, slash + 1);
}

static void show_track_list(void)
{
    nas_entry_t *tracks;
    int count;
    if (!player_copy_tracks(&tracks, &count)) return;
    player_status_t st;
    player_get_status(&st);
    list_view_t *v = &s_tracks;
    list_clear(v);
    v->rows = calloc(count, sizeof(row_t));
    if (v->rows) {
        char text[256];
        for (int i = 0; i < count; i++) {
            row_t *r = &v->rows[i];
            r->kind = ROW_TRACK;
            r->track = i;
            display_name(tracks[i].name, text, sizeof(text), true);
            add_row(v, r, i, text, NULL);
        }
        v->count = count;
    }
    nas_free_entries(tracks, count);
    lv_label_set_text(v->title, lv_label_get_text(s_pl.album));  // the album, as in the player's top bar
    show_view(V_TRACKS);
    list_select_anim(v, st.track, LV_ANIM_OFF);
}

static void do_action(action_t a)
{
    switch (a) {
    case A_BACK:
        if (s_view == V_SEARCH) {
            show_view(s_search_return);
        } else if (s_view == V_TRACKS) {
            show_view(V_PLAYER);
        } else if (s_view == V_PLAYER) {
            show_view(V_BROWSE);
            // Refresh "Weiterhören" on the home list, keeping the selection where it was.
            if (strcmp(s_cur_path, NAS_ROOT) == 0) {
                const row_t *r = s_browse.sel >= 0 && s_browse.sel < s_browse.count ? &s_browse.rows[s_browse.sel] : NULL;
                nav_post(NAV_OPEN, NAS_ROOT, r && r->kind == ROW_DIR ? r->name : NULL);
            }
        } else if (s_view == V_RIPPER) {
            show_view(V_BROWSE);
        } else if (s_view == V_BROWSE) {
            go_up();
        }
        break;
    case A_TRACKS: show_track_list(); break;
    case A_PREV: player_step(-1); break;
    case A_REWIND: player_seek(-SEEK_STEP_S); break;
    case A_TOGGLE: player_toggle(); break;
    case A_FORWARD: player_seek(SEEK_STEP_S); break;
    case A_NEXT: player_step(1); break;
    case A_VOL_DOWN: player_set_volume(player_volume() - 1); break;
    case A_VOL_UP: player_set_volume(player_volume() + 1); break;
    case A_PLAYER: show_view(V_PLAYER); break;
    case A_RIPPER: show_view(V_RIPPER); break;
    case A_OUTPUT: {
        player_status_t st;
        player_get_status(&st);
        player_set_boombox(!st.boombox);
        toast(tr(st.boombox ? S_PLAYS_HERE : S_PLAYS_BOOMBOX));
        break;
    }
    }
}

static void row_activate(list_view_t *v, int i)
{
    if (i < 0 || i >= v->count) return;
    row_t *r = &v->rows[i];
    char path[NAS_PATH_MAX];
    switch (r->kind) {
    case ROW_DIR:
        if (snprintf(path, sizeof(path), "%s/%s", s_cur_path, r->name) < (int)sizeof(path)) nav_post(NAV_OPEN, path, NULL);
        break;
    case ROW_PLAY_ALL: nav_post(NAV_PLAY, s_cur_path, NULL); break;
    case ROW_RESUME: nav_post(NAV_PLAY, r->name, NULL); break;
    case ROW_TRACK:
        player_jump(r->track);
        show_view(V_PLAYER);
        break;
    case ROW_HIT:
        if (r->track == SEARCH_TRACK) {
            // Play the album, starting with this track.
            const char *slash = strrchr(r->name, '/');
            snprintf(path, sizeof(path), "%.*s", (int)(slash - r->name), r->name);
            nav_post(NAV_PLAY, path, slash + 1);
        } else {
            nav_post(r->track == SEARCH_ALBUM ? NAV_PLAY : NAV_OPEN, r->name, NULL);
        }
        break;
    case ROW_INFO:
        break;
    case ROW_RESYNC:
        if (index_progress() < 0) {
            s_resync_asked = true;
            index_request_refresh();
            lv_label_set_text_fmt(r->label, LV_SYMBOL_REFRESH " %s", tr(S_READING));
        }
        break;
    }
}

static bool wake(void)
{
    s_last_input = esp_timer_get_time();
    if (s_screen_on) return false;
    s_screen_on = true;
    board_backlight(true);
    return true;
}

static void on_row(lv_event_t *e)
{
    list_view_t *v = s_view == V_TRACKS ? &s_tracks : s_view == V_SEARCH ? &s_search : &s_browse;
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    list_select(v, i);
    row_activate(v, i);
}

static void on_action(lv_event_t *e) { do_action((action_t)(intptr_t)lv_event_get_user_data(e)); }

static void add_row(list_view_t *v, row_t *r, int index, const char *text, const char *sub)
{
    lv_obj_t *b = lv_button_create(v->list);
    lv_obj_set_size(b, lv_pct(100), sub ? ROW_H_TWO_LINES : ROW_H);
    lv_obj_set_style_bg_color(b, COLOR_ROW, 0);
    lv_obj_set_style_bg_color(b, COLOR_SEL, LV_STATE_CHECKED);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_style_radius(b, 8, 0);
    lv_obj_set_style_pad_hor(b, 10, 0);
    lv_obj_add_event_cb(b, on_row, LV_EVENT_CLICKED, (void *)(intptr_t)index);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, text);
    lv_obj_set_width(l, lv_pct(100));
    lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(l, &font_roboto_20, 0);
    one_line(l);
    if (sub) {
        lv_obj_align(l, LV_ALIGN_TOP_LEFT, 0, -4);
        lv_obj_t *s2 = lv_label_create(b);
        lv_label_set_text(s2, sub);
        lv_obj_set_width(s2, lv_pct(100));
        lv_label_set_long_mode(s2, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_font(s2, &font_roboto_16, 0);
        one_line(s2);
        lv_obj_set_style_text_color(s2, COLOR_DIM, 0);
        lv_obj_set_style_text_color(s2, COLOR_TEXT, LV_STATE_CHECKED);
        lv_obj_align(s2, LV_ALIGN_BOTTOM_LEFT, 0, 4);
    } else {
        lv_obj_center(l);
    }
    r->obj = b;
    r->label = l;
}

static lv_obj_t *make_button(lv_obj_t *parent, int x, int y, int w, int h, const char *text, action_t a)
{
    lv_obj_t *b = lv_button_create(parent);
    lv_obj_set_pos(b, x, y);
    lv_obj_set_size(b, w, h);
    lv_obj_set_style_bg_color(b, COLOR_ROW, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_style_radius(b, 10, 0);
    lv_obj_set_style_pad_all(b, 0, 0);
    lv_obj_add_event_cb(b, on_action, LV_EVENT_CLICKED, (void *)(intptr_t)a);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, &font_roboto_20, 0);
    lv_obj_center(l);
    return b;
}

static lv_obj_t *make_page(lv_obj_t *parent)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, lv_pct(100), lv_pct(100));
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
    return o;
}

static lv_obj_t *make_label(lv_obj_t *parent, int x, int y, int w, const lv_font_t *font, lv_color_t color)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, "");
    lv_obj_set_pos(l, x, y);
    if (w) lv_obj_set_width(l, w);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    return l;
}

// One line, with "…" or scrolling: LVGL only stops wrapping a label whose height is fixed.
static void one_line(lv_obj_t *l)
{
    lv_obj_set_height(l, lv_font_get_line_height(lv_obj_get_style_text_font(l, LV_PART_MAIN)));
}

static void list_view_create(list_view_t *v, lv_obj_t *parent)
{
    v->root = make_page(parent);
    v->sel = -1;
    v->back = make_button(v->root, 4, 2, 44, HEADER_H - 4, LV_SYMBOL_LEFT, A_BACK);
    // The home list has no back button; the CD ripper takes its place there.
    v->ripper = make_button(v->root, 4, 2, 44, HEADER_H - 4, LV_SYMBOL_DRIVE, A_RIPPER);
    lv_obj_add_flag(v->ripper, LV_OBJ_FLAG_HIDDEN);
    v->title = make_label(v->root, 56, 8, 156, &font_roboto_16, COLOR_DIM);
    lv_label_set_long_mode(v->title, LV_LABEL_LONG_DOT);
    one_line(v->title);
    v->battery = make_label(v->root, 216, 8, 52, &font_roboto_16, COLOR_DIM);
    // Mirrors the list button on the player: back to what is playing. Shown once an album is loaded.
    v->player = make_button(v->root, 272, 2, 44, HEADER_H - 4, LV_SYMBOL_AUDIO, A_PLAYER);
    lv_obj_add_flag(v->player, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_text_align(v->battery, LV_TEXT_ALIGN_RIGHT, 0);
    v->list = lv_obj_create(v->root);
    lv_obj_remove_style_all(v->list);
    lv_obj_set_pos(v->list, 4, HEADER_H);
    lv_obj_set_size(v->list, 312, 240 - HEADER_H);
    lv_obj_set_flex_flow(v->list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(v->list, 4, 0);
    lv_obj_set_style_pad_bottom(v->list, 4, 0);
    lv_obj_add_flag(v->list, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(v->list, LV_SCROLLBAR_MODE_OFF);
}

static void player_view_create(lv_obj_t *parent)
{
    lv_obj_t *p = s_pl.root = make_page(parent);
    make_button(p, 4, 2, 44, HEADER_H - 4, LV_SYMBOL_LEFT, A_BACK);
    make_button(p, 272, 2, 44, HEADER_H - 4, LV_SYMBOL_LIST, A_TRACKS);
    s_pl.album = make_label(p, 56, 8, 160, &font_roboto_16, COLOR_DIM);
    lv_label_set_long_mode(s_pl.album, LV_LABEL_LONG_DOT);
    one_line(s_pl.album);
    s_pl.battery = make_label(p, 216, 8, 52, &font_roboto_16, COLOR_DIM);
    lv_obj_set_style_text_align(s_pl.battery, LV_TEXT_ALIGN_RIGHT, 0);
    s_pl.title = make_label(p, 10, 44, 300, &font_roboto_20, COLOR_TEXT);
    lv_label_set_long_mode(s_pl.title, LV_LABEL_LONG_SCROLL_CIRCULAR);
    s_pl.info = make_label(p, 10, 70, 300, &font_roboto_16, COLOR_ACCENT);

    s_pl.bar = lv_bar_create(p);
    lv_obj_set_pos(s_pl.bar, 10, 96);
    lv_obj_set_size(s_pl.bar, 300, 10);
    lv_bar_set_range(s_pl.bar, 0, 1000);
    lv_obj_set_style_bg_color(s_pl.bar, COLOR_ROW, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_pl.bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_pl.bar, COLOR_ACCENT, LV_PART_INDICATOR);
    s_pl.pos = make_label(p, 10, 110, 100, &font_roboto_16, COLOR_DIM);
    s_pl.dur = make_label(p, 210, 110, 100, &font_roboto_16, COLOR_DIM);
    lv_obj_set_style_text_align(s_pl.dur, LV_TEXT_ALIGN_RIGHT, 0);

    make_button(p, 6, 134, 56, 56, LV_SYMBOL_PREV, A_PREV);
    make_button(p, 68, 134, 56, 56, "-30", A_REWIND);
    s_pl.toggle = make_button(p, 130, 134, 60, 56, LV_SYMBOL_PLAY, A_TOGGLE);
    lv_obj_set_style_bg_color(s_pl.toggle, COLOR_SEL, 0);
    make_button(p, 196, 134, 56, 56, "+30", A_FORWARD);
    make_button(p, 258, 134, 56, 56, LV_SYMBOL_NEXT, A_NEXT);

    make_button(p, 6, 198, 56, 38, LV_SYMBOL_VOLUME_MID, A_VOL_DOWN);
    s_pl.vol = lv_bar_create(p);
    lv_obj_set_pos(s_pl.vol, 70, 212);
    lv_obj_set_size(s_pl.vol, 124, 10);
    lv_bar_set_range(s_pl.vol, 0, PLAYER_VOLUME_MAX);
    lv_obj_set_style_bg_color(s_pl.vol, COLOR_ROW, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_pl.vol, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_pl.vol, COLOR_TEXT, LV_PART_INDICATOR);
    make_button(p, 258, 198, 56, 38, LV_SYMBOL_VOLUME_MAX, A_VOL_UP);
    // Output: the built-in speaker, or the boombox at the CD ripper (highlighted while it is the output).
    s_pl.output = make_button(p, 202, 198, 50, 38, LV_SYMBOL_BLUETOOTH, A_OUTPUT);
    if (!boombox_available()) {  // no boombox set up: the volume bar takes the room
        lv_obj_add_flag(s_pl.output, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_width(s_pl.vol, 176);
    }
}

static void rip_focus(int i)
{
    if (i < 0 || i > RIP_FORCE || lv_obj_has_flag(s_rip.buttons[i], LV_OBJ_FLAG_HIDDEN)) return;
    lv_obj_set_style_border_width(s_rip.buttons[s_rip.focus], 0, 0);
    s_rip.focus = i;
    lv_obj_set_style_border_width(s_rip.buttons[i], 2, 0);
}

static void rip_show_type(int chosen)
{
    for (int k = 0; k < RIP_CLS_COUNT; k++) lv_obj_set_style_bg_color(s_rip.buttons[k], k == chosen ? COLOR_SEL : COLOR_ROW, 0);
}

static void rip_press(int i)
{
    const ripper_status_t *st = &s_rip.st;
    if (i == RIP_EJECT) {
        ripper_eject();
        toast(tr(S_EJECTING));
    } else if (i == RIP_FORCE) {
        ripper_set_force(!st->force);
    } else if (!st->online || st->state != RIP_RIPPING) {
        toast(tr(S_TYPE_ONLY_WHILE_RIPPING));
    } else if (!(st->choices & (1u << i))) {
        toast(tr(S_TYPE_WRONG_FORMAT));
    } else {
        ripper_correct((rip_cls_t)i);
        rip_show_type(i);  // shown at once; the next poll confirms it
    }
}

static void on_rip_button(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    rip_focus(i);
    rip_press(i);
}

static void ripper_view_create(lv_obj_t *parent)
{
    lv_obj_t *p = s_rip.root = make_page(parent);
    make_button(p, 4, 2, 44, HEADER_H - 4, LV_SYMBOL_LEFT, A_BACK);
    lv_obj_t *t = make_label(p, 56, 8, 156, &font_roboto_16, COLOR_DIM);
    lv_label_set_text(t, tr(S_CD_RIPPER));
    s_rip.battery = make_label(p, 216, 8, 52, &font_roboto_16, COLOR_DIM);
    lv_obj_set_style_text_align(s_rip.battery, LV_TEXT_ALIGN_RIGHT, 0);
    s_rip.state = make_label(p, 10, 40, 300, &font_roboto_20, COLOR_ACCENT);
    s_rip.title = make_label(p, 10, 66, 300, &font_roboto_16, COLOR_TEXT);
    lv_label_set_long_mode(s_rip.title, LV_LABEL_LONG_SCROLL_CIRCULAR);
    s_rip.sub = make_label(p, 10, 86, 300, &font_roboto_16, COLOR_DIM);
    lv_label_set_long_mode(s_rip.sub, LV_LABEL_LONG_DOT);
    one_line(s_rip.sub);
    s_rip.bar = lv_bar_create(p);
    lv_obj_set_pos(s_rip.bar, 10, 110);
    lv_obj_set_size(s_rip.bar, 300, 8);
    lv_bar_set_range(s_rip.bar, 0, 100);
    lv_obj_set_style_bg_color(s_rip.bar, COLOR_ROW, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_rip.bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_rip.bar, COLOR_ACCENT, LV_PART_INDICATOR);
    s_rip.type = make_label(p, 10, 124, 300, &font_roboto_16, COLOR_DIM);
    lv_label_set_long_mode(s_rip.type, LV_LABEL_LONG_DOT);
    one_line(s_rip.type);

    static const str_t labels[RIP_FORCE + 1] = {S_BTN_KIDS, S_CLS_SPOKEN, S_CLS_MUSIC, S_CLS_UNSORTED, S_EJECT, S_RIP_ANYWAY};
    for (int i = 0; i <= RIP_FORCE; i++) {
        bool type = i < RIP_CLS_COUNT;
        lv_obj_t *b = lv_button_create(p);
        lv_obj_set_pos(b, type ? 4 + i * 78 : i == RIP_EJECT ? 4 : 142, type ? 144 : 190);
        lv_obj_set_size(b, type ? 75 : i == RIP_EJECT ? 128 : 174, type ? 38 : 44);
        lv_obj_set_style_bg_color(b, COLOR_ROW, 0);
        lv_obj_set_style_shadow_width(b, 0, 0);
        lv_obj_set_style_radius(b, 8, 0);
        lv_obj_set_style_pad_all(b, 0, 0);
        lv_obj_set_style_border_color(b, COLOR_ACCENT, 0);
        lv_obj_set_style_border_width(b, 0, 0);
        lv_obj_add_event_cb(b, on_rip_button, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        lv_obj_t *l = lv_label_create(b);
        if (i == RIP_EJECT) lv_label_set_text_fmt(l, LV_SYMBOL_EJECT " %s", tr(labels[i]));
        else lv_label_set_text(l, tr(labels[i]));
        lv_obj_set_style_text_font(l, &font_roboto_16, 0);
        lv_obj_center(l);
        s_rip.buttons[i] = b;
    }
    // Only offered when a disc was skipped as already there (or the wish is pending): in place of "Zuletzt".
    lv_obj_add_flag(s_rip.buttons[RIP_FORCE], LV_OBJ_FLAG_HIDDEN);
    s_rip.last_head = make_label(p, 142, 192, 172, &font_roboto_16, COLOR_DIM);
    lv_label_set_text(s_rip.last_head, tr(S_LAST_DONE));
    s_rip.last = make_label(p, 142, 212, 172, &font_roboto_16, COLOR_TEXT);
    lv_label_set_long_mode(s_rip.last, LV_LABEL_LONG_SCROLL_CIRCULAR);
    rip_focus(0);
}

static void set_text(lv_obj_t *l, const char *text)
{
    if (strcmp(lv_label_get_text(l), text)) lv_label_set_text(l, text);
}

static void format_time(char *out, size_t n, int s)
{
    if (s >= 3600) snprintf(out, n, "%d:%02d:%02d", s / 3600, s / 60 % 60, s % 60);
    else snprintf(out, n, "%d:%02d", s / 60, s % 60);
}

// Progress on the "neu einlesen" row, and what a finished crawl means for the screen.
static void update_resync(void)
{
    static uint32_t seen;
    list_view_t *v = &s_browse;
    int progress = index_progress();
    if (v->count && v->rows[v->count - 1].kind == ROW_RESYNC) {
        char text[64];
        if (progress >= 0) symbol_text(text, sizeof(text), LV_SYMBOL_REFRESH, tr(S_READING_N), progress);
        else symbol_text(text, sizeof(text), LV_SYMBOL_REFRESH, "%s", tr(S_RESCAN));
        set_text(v->rows[v->count - 1].label, text);
    }
    uint32_t done = index_crawls();
    if (done == seen) return;
    seen = done;
    index_result_t r = index_last_result();
    if (s_resync_asked) {
        toast(tr(r == INDEX_UPDATED ? S_LIB_UPDATED : r == INDEX_UNCHANGED ? S_NOTHING_NEW : S_RESCAN_FAILED));
        s_resync_asked = false;
    }
    // New albums show up in the open list right away; the selection stays where it is.
    if (r == INDEX_UPDATED && s_view == V_BROWSE) {
        const row_t *sel = v->sel >= 0 && v->sel < v->count ? &v->rows[v->sel] : NULL;
        nav_post(NAV_OPEN, s_cur_path, sel && sel->kind == ROW_DIR ? sel->name : NULL);
    }
}

// The CD-Ripper row on the home list, its screen, and news of finished discs.
static void update_ripper(void)
{
    static uint32_t finished;
    static bool first = true;
    ripper_status_t st;
    char text[300], reply[96];
    ripper_get(&st);

    if (!st.online) snprintf(text, sizeof(text), "%s", tr(st.seen ? S_NOT_REACHABLE : S_SEARCHING_RIPPER));
    else if (st.progress >= 0) snprintf(text, sizeof(text), "%s %d %%", ripper_state_name(st.state), st.progress);
    else snprintf(text, sizeof(text), "%s", ripper_state_name(st.state));

    // The header button glows while a disc is being read, ripped or copied.
    bool busy = st.online && (st.state == RIP_READING || st.state == RIP_RIPPING || st.state == RIP_DELIVERING);
    lv_obj_set_style_bg_color(s_browse.ripper, busy ? COLOR_ACCENT : COLOR_ROW, 0);
    lv_obj_set_style_text_color(s_browse.ripper, busy ? lv_color_black() : COLOR_TEXT, 0);

    if (first || st.finished != finished) {
        if (!first && st.finished != finished) {
            snprintf(text, sizeof(text), tr(st.last_kids ? S_NEW_DRAMA : S_CD_DONE), st.last);
            toast(text);
        }
        finished = st.finished;
        first = false;
    }
    if (ripper_take_reply(reply, sizeof(reply))) toast(reply);
    s_rip.st = st;
    if (s_view != V_RIPPER) return;

    set_text(s_rip.state, text);
    if (st.online && (busy || st.state == RIP_DONE || st.state == RIP_ERROR) && (st.artist[0] || st.album[0]))
        snprintf(text, sizeof(text), "%s%s%s", st.artist, st.artist[0] && st.album[0] ? " \xE2\x80\x93 " : "", st.album);
    else text[0] = 0;
    set_text(s_rip.title, text);
    if (!st.online) snprintf(text, sizeof(text), "%s", tr(S_PI_ON));
    else if (st.message[0] && st.cls[0]) snprintf(text, sizeof(text), "%s \xC2\xB7 %s", st.cls, st.message);
    else if (st.message[0] || st.cls[0]) snprintf(text, sizeof(text), "%s", st.message[0] ? st.message : st.cls);
    else if (st.waiting || st.failed) snprintf(text, sizeof(text), "%d warten aufs NAS, %d fehlgeschlagen", st.waiting, st.failed);
    else text[0] = 0;
    set_text(s_rip.sub, text);
    if (st.progress >= 0) {
        lv_obj_remove_flag(s_rip.bar, LV_OBJ_FLAG_HIDDEN);
        lv_bar_set_value(s_rip.bar, st.progress, LV_ANIM_OFF);
    } else {
        lv_obj_add_flag(s_rip.bar, LV_OBJ_FLAG_HIDDEN);
    }

    // The type of the disc being ripped can be corrected while it rips, within what its formats allow.
    bool ripping = st.online && st.state == RIP_RIPPING;
    if (!ripping) snprintf(text, sizeof(text), "%s", tr(S_TYPE_CHANGE_WHILE));
    else if (st.chosen != st.detected && st.detected != RIP_CLS_NONE)
        snprintf(text, sizeof(text), tr(S_TYPE_CHANGED), ripper_cls_name((rip_cls_t)st.detected));
    else snprintf(text, sizeof(text), "%s", tr(S_TYPE_OF_CD));
    if (ripping && st.release_pending) strlcat(text, tr(S_OTHER_ALBUM), sizeof(text));
    set_text(s_rip.type, text);
    rip_show_type(ripping ? st.chosen : RIP_CLS_NONE);
    for (int k = 0; k < RIP_CLS_COUNT; k++) {
        bool usable = ripping && (st.choices & (1u << k));
        lv_obj_set_style_text_color(s_rip.buttons[k], usable ? COLOR_TEXT : COLOR_DIM, 0);
    }

    bool offer = st.online && (st.already || st.force);
    lv_obj_t *fb = s_rip.buttons[RIP_FORCE];
    if (offer) {
        lv_obj_remove_flag(fb, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_rip.last_head, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_rip.last, LV_OBJ_FLAG_HIDDEN);
        char label[48];
        if (st.force) symbol_text(label, sizeof(label), LV_SYMBOL_OK, "%s", tr(S_WILL_RIP));
        else snprintf(label, sizeof(label), "%s", tr(S_RIP_ANYWAY));
        set_text(lv_obj_get_child(fb, 0), label);
        lv_obj_set_style_bg_color(fb, st.force ? COLOR_SEL : COLOR_ROW, 0);
    } else {
        if (s_rip.focus == RIP_FORCE) rip_focus(RIP_EJECT);
        lv_obj_add_flag(fb, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_rip.last_head, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_rip.last, LV_OBJ_FLAG_HIDDEN);
    }
    set_text(s_rip.last, st.last[0] ? st.last : "\xE2\x80\x93");
}

static void update_player(void)
{
    static player_status_t st;
    char text[280], a[16], b[16];
    player_get_status(&st);

    // The track list is opened from the player and returns there with back: no player button needed.
    bool loaded = st.album[0] != 0;
    list_view_t *lists[] = {&s_browse, &s_search};
    for (int i = 0; i < 2; i++) {
        if (loaded == lv_obj_has_flag(lists[i]->player, LV_OBJ_FLAG_HIDDEN)) {
            if (loaded) lv_obj_remove_flag(lists[i]->player, LV_OBJ_FLAG_HIDDEN);
            else lv_obj_add_flag(lists[i]->player, LV_OBJ_FLAG_HIDDEN);
        }
    }

    const char *base = strrchr(st.album, '/');
    display_name(base ? base + 1 : st.album, text, sizeof(text), false);
    set_text(s_pl.album, text);
    set_text(s_pl.title, st.track_name);

    if (st.boombox && st.message[0] && (st.state == PLAYER_ERROR || st.state == PLAYER_BUFFERING || st.offline))
        snprintf(text, sizeof(text), "%s", st.message);
    else if (st.offline) snprintf(text, sizeof(text), "%s", tr(S_NO_CONNECTION));
    else if (st.state == PLAYER_BUFFERING) snprintf(text, sizeof(text), "%s", tr(S_LOADING));
    else if (st.state == PLAYER_ERROR) snprintf(text, sizeof(text), "%s", tr(S_FILE_UNREADABLE));
    else if (st.finished) snprintf(text, sizeof(text), "%s", tr(S_FINISHED));
    else if (st.boombox) snprintf(text, sizeof(text), tr(S_TRACK_OF_BOOMBOX), st.track + 1, st.track_count);
    else snprintf(text, sizeof(text), tr(S_TRACK_OF), st.track + 1, st.track_count);
    set_text(s_pl.info, text);

    format_time(a, sizeof(a), st.pos_s);
    format_time(b, sizeof(b), st.dur_s);
    set_text(s_pl.pos, a);
    set_text(s_pl.dur, b);
    lv_bar_set_value(s_pl.bar, st.dur_s ? (int)((int64_t)st.pos_s * 1000 / st.dur_s) : 0, LV_ANIM_OFF);
    lv_bar_set_value(s_pl.vol, player_volume(), LV_ANIM_OFF);
    bool running = st.state == PLAYER_PLAYING || st.state == PLAYER_BUFFERING;
    set_text(lv_obj_get_child(s_pl.toggle, 0), running ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);
    if (st.boombox != s_pl.boombox) {  // the button starts as "speaker"
        s_pl.boombox = st.boombox;
        lv_obj_set_style_bg_color(s_pl.output, st.boombox ? COLOR_SEL : COLOR_ROW, 0);
    }
}

static void update_battery(void)
{
    int pct = board_battery_percent();
    char text[16] = "";
    if (pct >= 0) snprintf(text, sizeof(text), "%d%%", pct);
    set_text(s_pl.battery, text);
    set_text(s_browse.battery, text);
    set_text(s_tracks.battery, text);
    set_text(s_search.battery, text);
    set_text(s_rip.battery, text);
}

// Name of the folder or file at the end of path, and of the folder holding it.
static void hit_texts(const search_hit_t *h, char *name, size_t name_size, char *sub, size_t sub_size)
{
    const char *base = strrchr(h->path, '/');
    display_name(base + 1, name, name_size, h->kind == SEARCH_TRACK);
    if (h->kind == SEARCH_FOLDER) strlcat(name, " \xE2\x80\xA6", name_size);
    const char *p = base;
    while (p > h->path && p[-1] != '/') p--;
    if (p <= h->path + 1 || base - h->path <= (long)strlen(NAS_ROOT)) {
        snprintf(sub, sub_size, "%s", tr(S_LIBRARY));
        return;
    }
    char parent[256];
    snprintf(parent, sizeof(parent), "%.*s", (int)(base - p), p);
    display_name(parent, sub, sub_size, false);
}

static void search_info(list_view_t *v, const char *text)
{
    v->rows = calloc(1, sizeof(row_t));
    if (!v->rows) return;
    v->rows[0].kind = ROW_INFO;
    add_row(v, &v->rows[0], 0, text, NULL);
    lv_obj_set_style_text_color(v->rows[0].label, COLOR_DIM, 0);
    v->count = 1;
}

static void search_run(lv_timer_t *t)
{
    static search_hit_t hits[SEARCH_MAX_HITS];
    list_view_t *v = &s_search;
    lv_timer_pause(t);
    list_clear(v);
    if (!s_query[0]) return;

    int64_t t0 = esp_timer_get_time();
    search_index_t *idx = index_acquire();
    int n = idx ? search_query(idx, s_query, hits, SEARCH_MAX_HITS) : 0;
    ESP_LOGI(TAG, "search '%s': %d hits in %d ms", s_query, n, (int)((esp_timer_get_time() - t0) / 1000));
    if (n) {
        v->rows = calloc(n, sizeof(row_t));
        char name[256], sub[256];
        for (int i = 0; v->rows && i < n; i++) {
            row_t *r = &v->rows[i];
            r->kind = ROW_HIT;
            r->track = hits[i].kind;
            r->name = strdup(hits[i].path);
            hit_texts(&hits[i], name, sizeof(name), sub, sizeof(sub));
            add_row(v, r, i, name, sub);
            v->count = i + 1;
        }
    }
    index_release();

    char text[64];
    if (!idx) {
        int done = index_progress();
        snprintf(text, sizeof(text), tr(done >= 0 ? S_INDEX_BUILDING : S_INDEX_MISSING), done);
        search_info(v, text);
    } else if (!n) {
        search_info(v, tr(S_NOTHING_FOUND));
    }
    list_select_anim(v, 0, LV_ANIM_OFF);
}

static void search_update(void)
{
    char title[SEARCH_QUERY_MAX + 16];
    snprintf(title, sizeof(title), "%s_", s_query);
    set_text(s_search.title, title);
    lv_timer_reset(s_search_timer);
    lv_timer_resume(s_search_timer);
}

static void search_type(char ch)
{
    if (s_view != V_SEARCH) {
        s_search_return = s_view;
        s_query[0] = 0;
        list_clear(&s_search);
        show_view(V_SEARCH);
    }
    size_t n = strlen(s_query);
    if (ch == ' ' && (n == 0 || s_query[n - 1] == ' ')) return;
    if (n < SEARCH_QUERY_MAX) {
        s_query[n] = ch;
        s_query[n + 1] = 0;
    }
    search_update();
}

static void search_input(const board_event_t *ev)
{
    list_view_t *v = &s_search;
    size_t n = strlen(s_query);
    switch (ev->type) {
    case IN_CHAR: search_type(ev->ch); break;
    case IN_BACK:
        if (!n) {
            do_action(A_BACK);
            break;
        }
        s_query[n - 1] = 0;
        search_update();
        break;
    case IN_UP: list_select(v, v->sel - 1); break;
    case IN_DOWN: list_select(v, v->sel + 1); break;
    case IN_RIGHT:
    case IN_CLICK:
    case IN_ENTER: row_activate(v, v->sel); break;
    case IN_LEFT: do_action(A_BACK); break;
    default: break;
    }
}

static void handle_input(const board_event_t *ev)
{
    board_input_t in = ev->type;
    if (wake()) return;  // the first touch of a dark screen only turns it on
    if (s_view == V_SEARCH) {
        search_input(ev);
        return;
    }
    // Typing anywhere starts a search; space and Enter keep their old meaning.
    if (in == IN_CHAR && ev->ch != ' ' && s_view != V_BOOT) {
        search_type(ev->ch);
        return;
    }
    if (in == IN_ENTER || (in == IN_CHAR && ev->ch == ' ')) in = IN_CLICK;
    if (s_view == V_PLAYER) {
        switch (in) {
        case IN_UP: do_action(A_VOL_UP); break;
        case IN_DOWN: do_action(A_VOL_DOWN); break;
        case IN_LEFT: do_action(A_REWIND); break;
        case IN_RIGHT: do_action(A_FORWARD); break;
        case IN_CLICK: do_action(A_TOGGLE); break;
        case IN_BACK: do_action(A_BACK); break;
        default: break;
        }
    } else if (s_view == V_RIPPER) {
        switch (in) {
        case IN_LEFT:
            if (s_rip.focus == 0 || s_rip.focus == RIP_EJECT) do_action(A_BACK);
            else rip_focus(s_rip.focus - 1);
            break;
        case IN_RIGHT:
            if (s_rip.focus != RIP_CLS_COUNT - 1) rip_focus(s_rip.focus + 1);
            break;
        case IN_DOWN:
            if (s_rip.focus < RIP_CLS_COUNT) rip_focus(RIP_EJECT);
            break;
        case IN_UP:
            if (s_rip.focus >= RIP_CLS_COUNT) rip_focus(s_rip.st.chosen >= 0 ? s_rip.st.chosen : 0);
            break;
        case IN_CLICK: rip_press(s_rip.focus); break;
        case IN_BACK: do_action(A_BACK); break;
        default: break;
        }
    } else if (s_view == V_BROWSE || s_view == V_TRACKS) {
        list_view_t *v = s_view == V_BROWSE ? &s_browse : &s_tracks;
        switch (in) {
        case IN_UP: list_select(v, v->sel - 1); break;
        case IN_DOWN: list_select(v, v->sel + 1); break;
        case IN_RIGHT:
        case IN_CLICK: row_activate(v, v->sel); break;
        case IN_LEFT:
        case IN_BACK: do_action(A_BACK); break;
        default: break;
        }
    }
}

static void tick(lv_timer_t *t)
{
    static int64_t next_battery, next_status;
    board_event_t ev;
    while (board_input_next(&ev)) handle_input(&ev);

    int64_t now = esp_timer_get_time();
    if (s_view != V_BOOT && now >= next_status) {
        next_status = now + 250000;
        update_resync();
        update_ripper();
        update_player();
    }
    if (s_view != V_BOOT && now >= next_battery) {
        next_battery = now + BATTERY_POLL_US;
        update_battery();
    }
    if (s_screen_on && now - s_last_input > SCREEN_OFF_US) {
        s_screen_on = false;
        board_backlight(false);
    }
}

static void touch_read(lv_indev_t *indev, lv_indev_data_t *data)
{
    static int last_x, last_y;
    int x, y;
    bool pressed = board_touch_read(&x, &y);
    data->state = LV_INDEV_STATE_RELEASED;
    data->point.x = last_x;
    data->point.y = last_y;
    if (!pressed) {
        s_touch_swallow = false;
        return;
    }
    if (wake()) s_touch_swallow = true;
    if (s_touch_swallow) return;
    last_x = data->point.x = x;
    last_y = data->point.y = y;
    data->state = LV_INDEV_STATE_PRESSED;
}

static void show_browse(const char *path, const nas_entry_t *entries, int count, int dirs, int audio, const char *focus)
{
    list_view_t *v = &s_browse;
    bool root = strcmp(path, NAS_ROOT) == 0;
    char last[NAS_PATH_MAX], text[300];
    bool has_last = root && store_get_last(last, sizeof(last));

    list_clear(v);
    strlcpy(s_cur_path, path, sizeof(s_cur_path));
    int capacity = dirs + 3, n = 0, want = -1;
    v->rows = calloc(capacity, sizeof(row_t));
    if (!v->rows) return;
    if (has_last) {
        const char *base = strrchr(last, '/');
        char name[256];
        display_name(base ? base + 1 : last, name, sizeof(name), false);
        symbol_text(text, sizeof(text), LV_SYMBOL_PLAY, tr(S_CONTINUE), name);
        v->rows[n] = (row_t){.kind = ROW_RESUME, .name = strdup(last)};
        add_row(v, &v->rows[n], n, text, NULL);
        n++;
    }
    if (audio) {
        symbol_text(text, sizeof(text), LV_SYMBOL_PLAY, "%s", tr(S_PLAY_ALL));
        v->rows[n] = (row_t){.kind = ROW_PLAY_ALL};
        add_row(v, &v->rows[n], n, text, NULL);
        n++;
    }
    for (int i = 0; i < count; i++) {
        if (!entries[i].is_dir) continue;
        if (focus && strcmp(entries[i].name, focus) == 0) want = n;
        display_name(entries[i].name, text, sizeof(text), false);
        v->rows[n] = (row_t){.kind = ROW_DIR, .name = strdup(entries[i].name)};
        add_row(v, &v->rows[n], n, text, NULL);
        n++;
    }
    if (root) {
        v->rows[n] = (row_t){.kind = ROW_RESYNC};
        char rescan[64];
        symbol_text(rescan, sizeof(rescan), LV_SYMBOL_REFRESH, "%s", tr(S_RESCAN));
        add_row(v, &v->rows[n], n, rescan, NULL);
        lv_obj_set_style_text_color(v->rows[n].label, COLOR_DIM, 0);
        n++;
    }
    v->count = n;

    const char *base = strrchr(path, '/');
    display_name(root ? tr(S_LIBRARY) : base + 1, text, sizeof(text), false);
    set_text(v->title, text);
    lv_obj_add_flag(root ? v->back : v->ripper, LV_OBJ_FLAG_HIDDEN);
    if (!root || ripper_available()) lv_obj_remove_flag(root ? v->ripper : v->back, LV_OBJ_FLAG_HIDDEN);
    if (s_view != V_PLAYER && s_view != V_TRACKS) show_view(V_BROWSE);
    list_select_anim(v, want >= 0 ? want : 0, LV_ANIM_OFF);
}

static void start_album(const char *path, nas_entry_t *entries, int count, const char *start_name)
{
    player_status_t st;
    player_get_status(&st);
    if (strcmp(st.album, path) == 0 && st.state != PLAYER_STOPPED) {  // already loaded: just show it
        if (start_name) {
            for (int i = 0, k = 0; i < count; i++) {
                if (entries[i].is_dir || !is_audio_name(entries[i].name)) continue;
                if (strcmp(entries[i].name, start_name) == 0 && k != st.track) player_jump(k);
                k++;
            }
        }
        return;
    }

    nas_entry_t *tracks = calloc(count, sizeof(*tracks));
    if (!tracks) return;
    int n = 0;
    for (int i = 0; i < count; i++) {
        if (entries[i].is_dir || !is_audio_name(entries[i].name)) continue;
        tracks[n] = entries[i];
        tracks[n++].name = strdup(entries[i].name);
    }
    int track = 0;
    uint32_t offset = 0, bps = 0;
    store_pos_t pos;
    if (store_get_pos(path, &pos)) {
        for (int i = 0; i < n; i++) {
            if (fnv1a(tracks[i].name) == pos.track_hash) {
                track = i;
                offset = pos.offset;
                bps = pos.bytes_per_sec;
            }
        }
    }
    // A track picked in search starts from its beginning, unless that is where the album stopped anyway.
    for (int i = 0; start_name && i < n; i++) {
        if (strcmp(tracks[i].name, start_name) == 0 && i != track) {
            track = i;
            offset = bps = 0;
        }
    }
    store_set_last(path);
    player_play(path, tracks, n, track, offset, bps);
}

// The index answers in milliseconds. The NAS, for folders the index does not know yet, over one
// connection kept open between requests.
static nas_err_t nav_list(const char *path, nas_entry_t **entries, int *count, const char **source)
{
    static nas_lister_t *lister;
    *source = "index";
    if (index_list(path, entries, count)) return NAS_OK;
    *source = "NAS";
    if (!lister) lister = nas_lister_new();
    if (!lister) return NAS_ERR_NET;
    nas_err_t err = nas_list_with(lister, path, entries, count);
    if (err == NAS_ERR_NET) {  // start over with a fresh connection next time
        nas_lister_free(lister);
        lister = NULL;
    }
    return err;
}

static void nav_handle(const nav_msg_t *m)
{
    nas_entry_t *entries = NULL;
    int count = 0;
    const char *source;
    int64_t t0 = esp_timer_get_time();
    nas_err_t err = nav_list(m->path, &entries, &count, &source);
    if (err != NAS_OK) {
        lvgl_port_lock(0);
        toast("NAS nicht erreichbar");
        lvgl_port_unlock();
        return;
    }
    int dirs = 0, audio = 0;
    for (int i = 0; i < count; i++) {
        if (entries[i].is_dir) dirs++;
        else if (is_audio_name(entries[i].name)) audio++;
    }
    ESP_LOGI(TAG, "%s: %d folders, %d tracks from %s in %d ms", m->path, dirs, audio, source, (int)((esp_timer_get_time() - t0) / 1000));
    if (audio && (m->type == NAV_PLAY || dirs == 0)) {
        start_album(m->path, entries, count, m->focus[0] ? m->focus : NULL);
        lvgl_port_lock(0);
        show_view(V_PLAYER);
        lvgl_port_unlock();
    } else if (!dirs && !audio) {
        lvgl_port_lock(0);
        toast(tr(S_NOTHING_TO_HEAR));
        lvgl_port_unlock();
    } else {
        lvgl_port_lock(0);
        show_browse(m->path, entries, count, dirs, audio, m->focus[0] ? m->focus : NULL);
        lvgl_port_unlock();
    }
    nas_free_entries(entries, count);
}

static void nav_task(void *arg)
{
    for (;;) {
        nav_msg_t *m;
        xQueueReceive(s_nav, &m, portMAX_DELAY);
        nav_handle(m);
        free(m);
    }
}

static void on_render(lv_event_t *e)
{
    static int64_t start;
    if (lv_event_get_code(e) == LV_EVENT_RENDER_START) {
        start = esp_timer_get_time();
    } else {
        int ms = (int)((esp_timer_get_time() - start) / 1000);
        if (ms > 50) ESP_LOGI(TAG, "slow frame: %d ms", ms);
    }
}

void ui_init(lv_display_t *disp)
{
    s_nav = xQueueCreate(4, sizeof(nav_msg_t *));
    s_last_input = esp_timer_get_time();
    lvgl_port_lock(0);
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, COLOR_BG, 0);
    lv_obj_set_style_text_color(scr, COLOR_TEXT, 0);
    lv_obj_set_style_text_font(scr, &font_roboto_16, 0);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    // Start screen: logo and name, below them what the device is doing (or where to set it up).
    s_boot = make_page(scr);
    lv_obj_t *logo = lv_image_create(s_boot);
    lv_image_set_src(logo, &logo_img);
    lv_obj_align(logo, LV_ALIGN_TOP_MID, 0, 10);
    lv_obj_t *name = make_label(s_boot, 0, 0, 300, &font_roboto_20, COLOR_BRAND);
    lv_label_set_text(name, "Fl\xC3\xBCstert\xC3\xBCte");
    lv_obj_set_style_text_align(name, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(name, LV_ALIGN_TOP_MID, 0, 92);
    s_boot_label = make_label(s_boot, 0, 0, 300, &font_roboto_16, COLOR_DIM);
    lv_obj_set_style_text_align(s_boot_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_boot_label, LV_ALIGN_TOP_MID, 0, 126);
    list_view_create(&s_browse, scr);
    list_view_create(&s_tracks, scr);
    list_view_create(&s_search, scr);
    lv_obj_set_style_text_font(s_search.title, &font_roboto_20, 0);
    lv_obj_set_style_text_color(s_search.title, COLOR_TEXT, 0);
    lv_obj_set_y(s_search.title, 6);
    s_search_timer = lv_timer_create(search_run, SEARCH_DELAY_MS, NULL);
    lv_timer_pause(s_search_timer);
    player_view_create(scr);
    ripper_view_create(scr);

    s_toast = lv_label_create(scr);
    lv_obj_set_style_bg_color(s_toast, COLOR_ACCENT, 0);
    lv_obj_set_style_bg_opa(s_toast, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(s_toast, lv_color_black(), 0);
    lv_obj_set_style_pad_all(s_toast, 8, 0);
    lv_obj_set_style_radius(s_toast, 6, 0);
    lv_obj_align(s_toast, LV_ALIGN_BOTTOM_MID, 0, -8);
    lv_obj_add_flag(s_toast, LV_OBJ_FLAG_HIDDEN);

    show_view(V_BOOT);
    lv_indev_t *touch = lv_indev_create();
    lv_indev_set_type(touch, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(touch, touch_read);
    lv_indev_set_display(touch, disp);
    lv_timer_create(tick, 30, NULL);
    lv_display_add_event_cb(disp, on_render, LV_EVENT_RENDER_START, NULL);
    lv_display_add_event_cb(disp, on_render, LV_EVENT_RENDER_READY, NULL);
    lvgl_port_unlock();
}

void ui_boot_message(const char *text)
{
    lvgl_port_lock(0);
    lv_label_set_text(s_boot_label, text);
    lvgl_port_unlock();
}

void ui_start(void)
{
#if CONFIG_SPIRAM_ALLOW_STACK_EXTERNAL_MEMORY
    xTaskCreatePinnedToCoreWithCaps(nav_task, "nav", 10240, NULL, 5, NULL, 0, MALLOC_CAP_SPIRAM);  // flash via flash_run()
#else
    xTaskCreatePinnedToCore(nav_task, "nav", 10240, NULL, 5, NULL, 0);
#endif
    nav_post(NAV_OPEN, NAS_ROOT, NULL);
}

