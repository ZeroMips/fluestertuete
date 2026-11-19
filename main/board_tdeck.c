#include "board.h"

#include <string.h>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/spi_master.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "board";

#define PIN_POWER 10
#define PIN_LORA_CS 9
#define PIN_SD_CS 39
#define PIN_I2C_SDA 18
#define PIN_I2C_SCL 8
#define PIN_SPI_CLK 40
#define PIN_SPI_MOSI 41
#define PIN_SPI_MISO 38
#define PIN_LCD_CS 12
#define PIN_LCD_DC 11
#define PIN_BACKLIGHT 42
#define PIN_BALL_UP 3
#define PIN_BALL_DOWN 15
#define PIN_BALL_LEFT 1
#define PIN_BALL_RIGHT 2
#define PIN_BALL_CLICK 0

#define LCD_W 320
#define LCD_H 240
#define KEYBOARD_ADDR 0x55
#define GT911_ADDR 0x5D
#define GT911_ADDR_ALT 0x14
#define BALL_EDGES_PER_STEP 2
#define KEYBOARD_POLL_US 25000

static i2c_master_bus_handle_t s_i2c;
static i2c_master_dev_handle_t s_touch, s_keyboard;
static int s_touch_xmax = LCD_H, s_touch_ymax = LCD_W;
static volatile int s_ball[4];
static adc_oneshot_unit_handle_t s_adc;
static adc_cali_handle_t s_adc_cali;

static void out_high(int pin)
{
    gpio_reset_pin(pin);
    gpio_set_direction(pin, GPIO_MODE_OUTPUT);
    gpio_set_level(pin, 1);
}

// The panel tuning the ESPHome build of this device runs with (ST7789V).
static void lcd_tune(esp_lcd_panel_io_handle_t io)
{
    static const struct {
        uint8_t cmd, len, data[14];
    } seq[] = {
        {0xB6, 2, {0x0A, 0x82}},
        {0xB2, 5, {0x0C, 0x0C, 0x00, 0x33, 0x33}},
        {0xB7, 1, {0x35}},
        {0xBB, 1, {0x28}},
        {0xC0, 1, {0x0C}},
        {0xC2, 2, {0x01, 0xFF}},
        {0xC3, 1, {0x10}},
        {0xC4, 1, {0x20}},
        {0xC6, 1, {0x0F}},
        {0xD0, 2, {0xA4, 0xA1}},
        {0xE0, 14, {0xD0, 0x00, 0x02, 0x07, 0x0A, 0x28, 0x32, 0x44, 0x42, 0x06, 0x0E, 0x12, 0x14, 0x17}},
        {0xE1, 14, {0xD0, 0x00, 0x02, 0x07, 0x0A, 0x28, 0x31, 0x54, 0x47, 0x0E, 0x1C, 0x17, 0x1B, 0x1E}},
    };
    for (size_t i = 0; i < sizeof(seq) / sizeof(seq[0]); i++) esp_lcd_panel_io_tx_param(io, seq[i].cmd, seq[i].data, seq[i].len);
}

static lv_display_t *lcd_init(void)
{
    spi_bus_config_t bus = {
        .sclk_io_num = PIN_SPI_CLK,
        .mosi_io_num = PIN_SPI_MOSI,
        .miso_io_num = PIN_SPI_MISO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = LCD_W * 40 * 2 + 8,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO));

    esp_lcd_panel_io_handle_t io;
    esp_lcd_panel_io_spi_config_t io_cfg = {
        .dc_gpio_num = PIN_LCD_DC,
        .cs_gpio_num = PIN_LCD_CS,
        .pclk_hz = 40 * 1000 * 1000,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .trans_queue_depth = 10,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST, &io_cfg, &io));

    esp_lcd_panel_handle_t panel;
    esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = -1,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR,
        .bits_per_pixel = 16,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(io, &panel_cfg, &panel));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel));
    lcd_tune(io);
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel, true));
    ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(panel, true));
    ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel, true, false));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel, true));

    lvgl_port_cfg_t port = ESP_LVGL_PORT_INIT_CONFIG();
    port.task_stack = 8192;
    port.task_affinity = 0;
    ESP_ERROR_CHECK(lvgl_port_init(&port));
    lvgl_port_display_cfg_t disp = {
        .io_handle = io,
        .panel_handle = panel,
        .buffer_size = LCD_W * 40,
        .double_buffer = true,
        .hres = LCD_W,
        .vres = LCD_H,
        .color_format = LV_COLOR_FORMAT_RGB565,
        .rotation = {.swap_xy = true, .mirror_x = true, .mirror_y = false},
        .flags = {.buff_dma = true, .swap_bytes = true},
    };
    return lvgl_port_add_disp(&disp);
}

static esp_err_t gt911_read(uint16_t reg, uint8_t *buf, size_t n)
{
    uint8_t addr[2] = {reg >> 8, reg & 0xFF};
    return i2c_master_transmit_receive(s_touch, addr, 2, buf, n, 50);
}

static void i2c_init(void)
{
    i2c_master_bus_config_t bus = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = PIN_I2C_SDA,
        .scl_io_num = PIN_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus, &s_i2c));
    i2c_device_config_t dev = {.dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = KEYBOARD_ADDR, .scl_speed_hz = 400000};
    ESP_ERROR_CHECK(i2c_master_bus_add_device(s_i2c, &dev, &s_keyboard));

    dev.device_address = i2c_master_probe(s_i2c, GT911_ADDR, 50) == ESP_OK ? GT911_ADDR : GT911_ADDR_ALT;
    ESP_ERROR_CHECK(i2c_master_bus_add_device(s_i2c, &dev, &s_touch));
    uint8_t max[4];
    if (gt911_read(0x8048, max, sizeof(max)) == ESP_OK) {
        int x = max[0] | max[1] << 8, y = max[2] | max[3] << 8;
        if (x > 0 && y > 0) {
            s_touch_xmax = x;
            s_touch_ymax = y;
        }
        ESP_LOGI(TAG, "touch at 0x%02x, range %dx%d", dev.device_address, x, y);
    } else {
        ESP_LOGW(TAG, "touch controller not answering");
    }
}

bool board_touch_read(int *x, int *y)
{
    uint8_t status, point[8];
    if (gt911_read(0x814E, &status, 1) != ESP_OK || !(status & 0x80)) return false;
    bool touched = (status & 0x0F) > 0 && gt911_read(0x814F, point, sizeof(point)) == ESP_OK;
    uint8_t clear[3] = {0x81, 0x4E, 0};
    i2c_master_transmit(s_touch, clear, sizeof(clear), 50);
    if (!touched) return false;
    int raw_x = point[1] | point[2] << 8, raw_y = point[3] | point[4] << 8;
    *x = raw_y * LCD_W / s_touch_ymax;            // same transform as the ESPHome config:
    *y = LCD_H - 1 - raw_x * LCD_H / s_touch_xmax;  // swap_xy with mirror_y
    if (*x >= LCD_W) *x = LCD_W - 1;
    if (*y < 0) *y = 0;
    return true;
}

static void IRAM_ATTR ball_isr(void *arg) { (*(volatile int *)arg)++; }

static void input_init(void)
{
    static const int pins[4] = {PIN_BALL_UP, PIN_BALL_DOWN, PIN_BALL_LEFT, PIN_BALL_RIGHT};
    gpio_config_t cfg = {.mode = GPIO_MODE_INPUT, .pull_up_en = true, .intr_type = GPIO_INTR_ANYEDGE};
    for (int i = 0; i < 4; i++) cfg.pin_bit_mask |= 1ULL << pins[i];
    ESP_ERROR_CHECK(gpio_config(&cfg));
    cfg.pin_bit_mask = 1ULL << PIN_BALL_CLICK;
    cfg.intr_type = GPIO_INTR_DISABLE;
    ESP_ERROR_CHECK(gpio_config(&cfg));
    ESP_ERROR_CHECK(gpio_install_isr_service(0));
    for (int i = 0; i < 4; i++) ESP_ERROR_CHECK(gpio_isr_handler_add(pins[i], ball_isr, (void *)&s_ball[i]));
}

bool board_input_next(board_event_t *ev)
{
    static const board_input_t dirs[4] = {IN_UP, IN_DOWN, IN_LEFT, IN_RIGHT};
    static bool click_was_down;
    static int64_t next_keyboard_poll;

    ev->ch = 0;
    for (int i = 0; i < 4; i++) {
        if (s_ball[i] >= BALL_EDGES_PER_STEP) {
            s_ball[i] -= BALL_EDGES_PER_STEP;
            ev->type = dirs[i];
            return true;
        }
    }
    bool down = gpio_get_level(PIN_BALL_CLICK) == 0;
    bool pressed = down && !click_was_down;
    click_was_down = down;
    if (pressed) {
        ev->type = IN_CLICK;
        return true;
    }

    int64_t now = esp_timer_get_time();
    if (now >= next_keyboard_poll) {
        next_keyboard_poll = now + KEYBOARD_POLL_US;
        uint8_t key = 0;
        if (i2c_master_receive(s_keyboard, &key, 1, 20) == ESP_OK && key) {
            ev->type = key == 0x08 ? IN_BACK : key == '\r' || key == '\n' ? IN_ENTER : key >= 0x20 && key < 0x7F ? IN_CHAR : IN_NONE;
            ev->ch = (char)key;
            return ev->type != IN_NONE;
        }
    }
    return false;
}

void board_backlight(bool on) { gpio_set_level(PIN_BACKLIGHT, on); }

static void battery_init(void)
{
    adc_oneshot_unit_init_cfg_t unit = {.unit_id = ADC_UNIT_1};
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&unit, &s_adc));
    adc_oneshot_chan_cfg_t chan = {.atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_DEFAULT};
    ESP_ERROR_CHECK(adc_oneshot_config_channel(s_adc, ADC_CHANNEL_3, &chan));  // GPIO4, behind a 1:2 divider
    adc_cali_curve_fitting_config_t cali = {.unit_id = ADC_UNIT_1, .atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_DEFAULT};
    if (adc_cali_create_scheme_curve_fitting(&cali, &s_adc_cali) != ESP_OK) s_adc_cali = NULL;
}

int board_battery_percent(void)
{
    int raw = 0, mv = 0, sum = 0;
    for (int i = 0; i < 8; i++) {
        if (adc_oneshot_read(s_adc, ADC_CHANNEL_3, &raw) != ESP_OK) return -1;
        sum += raw;
    }
    if (!s_adc_cali || adc_cali_raw_to_voltage(s_adc_cali, sum / 8, &mv) != ESP_OK) return -1;
    mv *= 2;
    if (mv < 2800 || mv > 4500) return -1;
    int pct = (mv - 3300) * 100 / (4150 - 3300);
    return pct < 0 ? 0 : pct > 100 ? 100 : pct;
}

lv_display_t *board_init(void)
{
    // The peripheral rail feeds display, keyboard and audio. The keyboard MCU needs time to boot.
    out_high(PIN_POWER);
    out_high(PIN_LORA_CS);
    out_high(PIN_SD_CS);
    gpio_reset_pin(PIN_BACKLIGHT);
    gpio_set_direction(PIN_BACKLIGHT, GPIO_MODE_OUTPUT);
    gpio_set_level(PIN_BACKLIGHT, 0);
    vTaskDelay(pdMS_TO_TICKS(800));

    i2c_init();
    input_init();
    battery_init();
    lv_display_t *disp = lcd_init();
    board_backlight(true);
    return disp;
}
