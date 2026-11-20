// Screenshots over the serial console: send 's' and the device prints the current screen as RGB565, base64
// encoded, between "SHOT <w> <h> <stride>" and "SHOT END". tools/screenshot.py turns that into a PNG.
// For the website and bug reports; costs one small task that polls the console.
#include <stdio.h>
#include <unistd.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "mbedtls/base64.h"
#include "screenshot.h"

static void dump(void)
{
    lvgl_port_lock(0);
    lv_draw_buf_t *buf = lv_snapshot_take(lv_screen_active(), LV_COLOR_FORMAT_RGB565);
    if (buf) {
        // Include whatever floats above the screen (messages): render the top layer onto the snapshot.
        lv_obj_t *top = lv_layer_top();
        if (lv_obj_get_child_count(top) > 0) {
            lv_draw_buf_t *over = lv_snapshot_take(top, LV_COLOR_FORMAT_ARGB8888);
            if (over) {
                for (uint32_t y = 0; y < buf->header.h && y < over->header.h; y++) {
                    uint16_t *dst = (uint16_t *)(buf->data + y * buf->header.stride);
                    const uint8_t *src = over->data + y * over->header.stride;
                    for (uint32_t x = 0; x < buf->header.w && x < over->header.w; x++) {
                        uint8_t b = src[4 * x], g = src[4 * x + 1], r = src[4 * x + 2], a = src[4 * x + 3];
                        if (!a) continue;
                        uint16_t d = dst[x];
                        uint8_t dr = (d >> 8) & 0xF8, dg = (d >> 3) & 0xFC, db = (d << 3) & 0xF8;
                        r = (r * a + dr * (255 - a)) / 255;
                        g = (g * a + dg * (255 - a)) / 255;
                        b = (b * a + db * (255 - a)) / 255;
                        dst[x] = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
                    }
                }
                lv_draw_buf_destroy(over);
            }
        }
    }
    lvgl_port_unlock();
    if (!buf) {
        printf("\nSHOT FAILED\n");
        return;
    }

    // Other tasks' log lines would land in the middle of the data: quiet while dumping. Each line still
    // carries its offset, so the reader can tell a damaged line (a printf from elsewhere) from a good one.
    esp_log_level_set("*", ESP_LOG_NONE);
    printf("\nSHOT %u %u %u\n", (unsigned)buf->header.w, (unsigned)buf->header.h, (unsigned)buf->header.stride);
    size_t total = buf->header.stride * buf->header.h;
    unsigned char line[73];
    for (size_t off = 0; off < total; off += 54) {
        size_t n = total - off < 54 ? total - off : 54, olen = 0;
        mbedtls_base64_encode(line, sizeof(line), &olen, buf->data + off, n);
        line[olen] = 0;
        printf("%x:%s\n", (unsigned)off, line);
        // Without a UART driver printing busy-waits: let the idle task in now and then, or the watchdog barks.
        if ((off / 54) % 8 == 7) vTaskDelay(1);
    }
    printf("SHOT END\n");
    fflush(stdout);
    esp_log_level_set("*", CONFIG_LOG_DEFAULT_LEVEL);
    lv_draw_buf_destroy(buf);
}

// LVGL renders the snapshot on this task's stack: it needs as much as the LVGL task itself.
static void task(void *arg)
{
    // The console has no driver installed: reads don't block and return 0 or -1 when nothing came in.
    for (;;) {
        char c;
        if (read(STDIN_FILENO, &c, 1) == 1) {
            if (c == 's') dump();
        } else {
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
}

void screenshot_init(void)
{
    // Lowest priority: the display stays live. On a screen that keeps scrolling a dump then takes a minute or two.
    xTaskCreatePinnedToCoreWithCaps(task, "shot", 16384, NULL, 1, NULL, 0, MALLOC_CAP_SPIRAM);
}
