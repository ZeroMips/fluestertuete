if(CONFIG_IDF_TARGET_ESP32)
    set(BOARD_SRC "board_core2.c")  # M5Stack Core2
else()
    set(BOARD_SRC "board_tdeck.c")  # LilyGo T-Deck
endif()
set(NAS_SRC "nas.c")
if(CONFIG_PLAYER_NAS_SYNOLOGY)
    list(APPEND NAS_SRC "nas_synology.c")
endif()
if(CONFIG_PLAYER_NAS_WEBDAV)
    list(APPEND NAS_SRC "nas_webdav.c" "davxml.c")
endif()
idf_component_register(
    SRCS "main.c" "${BOARD_SRC}" "net.c" ${NAS_SRC} "config.c" "i18n.c" "store.c" "player.c" "ui.c"
         "util.c" "ring.c" "flash_guard.c" "boombox.c" "ripper.c" "dlna.c" "mp3s.c" "search.c" "index.c"
         "fonts/font_roboto_16.c" "fonts/font_roboto_20.c"
    INCLUDE_DIRS "."
    REQUIRES esp_wifi esp_netif esp_event esp_http_client esp-tls mbedtls nvs_flash
             esp_driver_i2s esp_driver_i2c esp_driver_gpio esp_driver_spi esp_lcd esp_adc esp_timer esp_partition esp_rom json esp_app_format)
target_compile_definitions(${COMPONENT_LIB} PRIVATE LV_LVGL_H_INCLUDE_SIMPLE)
