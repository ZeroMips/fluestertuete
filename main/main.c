#include <stdio.h>
#include <string.h>

#include "board.h"
#include "config.h"
#include "esp_app_desc.h"
#include "flash_guard.h"
#include "i18n.h"
#include "index.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nas.h"
#include "net.h"
#include "player.h"
#include "ripper.h"
#include "store.h"
#include "ui.h"

// Internal RAM is what runs out first (sockets, TLS, Wi-Fi), above all on the Core2.
static void log_memory(void *arg)
{
    ESP_LOGI("mem", "internal free %u, largest %u, min %u | psram free %u",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
}

// Returns once everything runs: the main task ends and its stack goes back to the heap.
void app_main(void)
{
    flash_guard_init();
    store_init();
    config_load();
    i18n_set(strcmp(config()->lang, "en") == 0 ? LANG_EN : LANG_DE);
    nas_init();
    index_init();
    ui_init(board_init());

    if (!config()->valid || !nas_supported()) {
        // Nothing to connect to: say where to set the device up, and stay here.
        // Without any settings the language is unknown: then both.
        char text[320];
        str_t why = config()->valid ? S_FW_NO_NAS_TYPE : (str_t)(S_CFG_MISSING + config()->error - CFG_ERR_MISSING);
        if (config()->error == CFG_ERR_MISSING)
            snprintf(text, sizeof(text), "Fl\xC3\xBCstert\xC3\xBCte\n\n%s\n%s\n\n%s / %s\n%s\n\n%s \xC2\xB7 %s",
                     tr_lang(why, LANG_DE), tr_lang(why, LANG_EN), tr_lang(S_SETUP_AT, LANG_DE), tr_lang(S_SETUP_AT, LANG_EN),
                     CONFIG_PLAYER_SETUP_URL, BOARD_NAME, esp_app_get_description()->version);
        else
            snprintf(text, sizeof(text), "Fl\xC3\xBCstert\xC3\xBCte\n\n%s\n\n%s\n%s\n\n%s \xC2\xB7 %s", tr(why), tr(S_SETUP_AT),
                     CONFIG_PLAYER_SETUP_URL, BOARD_NAME, esp_app_get_description()->version);
        ui_boot_message(text);
        return;
    }

    ui_boot_message(tr(S_CONNECT_WIFI));
    net_start();
    while (!net_wait(30000)) ui_boot_message(tr(S_WIFI_NOT_FOUND));

    ui_boot_message(tr(S_CONNECT_NAS));
    for (;;) {
        nas_err_t err = nas_login();
        if (err == NAS_OK) break;
        ui_boot_message(tr(err == NAS_ERR_AUTH ? S_NAS_REFUSED : S_NAS_UNREACHABLE));
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
    player_init();
    ripper_start();
    ui_start();
    index_start_refresh();

    esp_timer_handle_t t;
    esp_timer_create_args_t args = {.callback = log_memory, .name = "mem"};
    if (esp_timer_create(&args, &t) == ESP_OK) esp_timer_start_periodic(t, 60 * 1000000LL);
}
