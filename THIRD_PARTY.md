# Third-party components

Flüstertüte itself is under the MIT license (`LICENSE`). The firmware and the setup website contain:

| Component | Used for | License |
|---|---|---|
| [ESP-IDF](https://github.com/espressif/esp-idf) 5.5 | firmware framework, Wi-Fi, TLS, drivers | Apache-2.0 (parts under other permissive licenses, see ESP-IDF) |
| [LVGL](https://lvgl.io) 9.4 | user interface | MIT |
| [esp_lvgl_port](https://components.espressif.com/components/espressif/esp_lvgl_port) 2.9 | LVGL on ESP-IDF displays | Apache-2.0 |
| [esp-libhelix-mp3](https://components.espressif.com/components/chmorgan/esp-libhelix-mp3) 1.0.3 (Helix MP3 decoder) | MP3 decoding | RealNetworks Public Source License 1.0 (RPSL) / RCSL; the wrapper Apache-2.0. Source: the component, unchanged |
| [cJSON](https://github.com/DaveGamble/cJSON) (in ESP-IDF) | JSON | MIT |
| [Roboto](https://fonts.google.com/specimen/Roboto) (16 and 20 px, converted with lv_font_conv) | text on the device | Apache-2.0 (SIL OFL 1.1 for newer releases) |
| [esptool-js](https://github.com/espressif/esptool-js) 0.7.0, `web/vendor/` | flashing from the browser | Apache-2.0 (`web/vendor/esptool-js-LICENSE`) |

The Helix decoder is used as published, without changes; RPSL section 2 asks for its notices to stay intact and
for its source to be available, which the component registry link above provides.
