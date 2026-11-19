// M5Stack Core2 v1.1: ESP32, AXP2101 power chip, ILI9342C display, FT6336U touch whose lower strip
// (y 240..279) holds the three touch buttons, NS4168 speaker amplifier. No keyboard, no trackball.
// Register values follow M5Stack's own M5GFX/M5Unified setup for this board.
#include "board.h"

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/spi_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "board";

#define PIN_I2C_SDA 21
#define PIN_I2C_SCL 22
#define PIN_SPI_CLK 18
#define PIN_SPI_MOSI 23
#define PIN_SPI_MISO 38
#define PIN_LCD_CS 5
#define PIN_LCD_DC 15
#define PIN_SD_CS 4

#define LCD_W 320
#define LCD_H 240
#define AXP_ADDR 0x34
#define TOUCH_ADDR 0x38
#define BUTTON_Y 240  // touch rows below the display are the buttons A, B, C

// AXP2101 registers
#define AXP_STATUS1 0x00
#define AXP_CHIP_ID 0x03
#define AXP_LDO_ON 0x90  // bit 0 ALDO1, 1 ALDO2 (LCD reset), 2 ALDO3 (speaker), 3 ALDO4 (LCD), 4 BLDO1 (backlight), 7 DLDO1 (vibration)
#define AXP_BATTERY_PERCENT 0xA4
#define LDO_ALDO2 0x02
#define LDO_ALDO3 0x04
#define LDO_ALDO4 0x08
#define LDO_BLDO1 0x10
#define LDO_DLDO1 0x80
#define LDO_3V3 28  // 0.5 V + 100 mV steps

static i2c_master_bus_handle_t s_i2c;
static i2c_master_dev_handle_t s_axp, s_touch;
static int s_button = -1;          // button held down, -1 none
static int s_button_pressed = -1;  // a new press for board_input_next()

static uint8_t axp_read(uint8_t reg)
{
    uint8_t v = 0;
    if (i2c_master_transmit_receive(s_axp, &reg, 1, &v, 1, 50) != ESP_OK) return 0;
    return v;
}

static void axp_write(uint8_t reg, uint8_t v)
{
    uint8_t b[2] = {reg, v};
    i2c_master_transmit(s_axp, b, 2, 50);
}

static void axp_bits(uint8_t reg, uint8_t mask, bool on)
{
    uint8_t v = axp_read(reg);
    axp_write(reg, on ? v | mask : v & ~mask);
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
    i2c_device_config_t dev = {.dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = AXP_ADDR, .scl_speed_hz = 400000};
    ESP_ERROR_CHECK(i2c_master_bus_add_device(s_i2c, &dev, &s_axp));
    dev.device_address = TOUCH_ADDR;
    ESP_ERROR_CHECK(i2c_master_bus_add_device(s_i2c, &dev, &s_touch));
}

static void power_init(void)
{
    uint8_t id = axp_read(AXP_CHIP_ID);
    if (id != 0x4A) ESP_LOGE(TAG, "power chip id 0x%02x, expected AXP2101 (0x4A): not a Core2 v1.1?", id);
    axp_write(0x27, 0x00);                 // power key: hold 1 s on, 4 s off
    axp_write(0x10, 0x30);                 // discharge rails when off
    axp_write(0x68, 0x01);                 // battery detection
    axp_write(0x69, 0x13);                 // charge LED
    axp_write(0x30, 0x0F);                 // ADCs on
    axp_bits(0x23, 0x1F, false);           // a DCDC dip resets the ESP32 instead of powering everything off
    axp_bits(0x80, 0x05, true);            // DCDC1, DCDC3 on
    axp_write(0x82, 0x12);                 // DCDC1 3.3 V
    axp_write(0x84, 0x6A);                 // DCDC3
    axp_bits(AXP_LDO_ON, LDO_DLDO1, false);  // vibration motor off
    axp_write(0x94, LDO_3V3);              // ALDO3: speaker amplifier
    axp_write(0x96, LDO_3V3);              // BLDO1: backlight, full brightness
    axp_bits(AXP_LDO_ON, LDO_ALDO4 | LDO_ALDO3, true);
    // Display reset is ALDO2.
    axp_bits(AXP_LDO_ON, LDO_ALDO2 | LDO_BLDO1, false);
    vTaskDelay(pdMS_TO_TICKS(20));
    axp_bits(AXP_LDO_ON, LDO_ALDO2, true);
    vTaskDelay(pdMS_TO_TICKS(120));
}

// ILI9342C power and gamma settings, from M5GFX's Panel_ILI9342.
static void lcd_tune(esp_lcd_panel_io_handle_t io)
{
    static const struct {
        uint8_t cmd, len, data[15];
    } seq[] = {
        {0xC8, 3, {0xFF, 0x93, 0x42}},
        {0xC0, 2, {0x12, 0x12}},
        {0xC1, 1, {0x03}},
        {0xC5, 1, {0xF2}},
        {0xB0, 1, {0xE0}},
        {0xF6, 3, {0x01, 0x00, 0x00}},
        {0xE0, 15, {0x00, 0x0C, 0x11, 0x04, 0x11, 0x08, 0x37, 0x89, 0x4C, 0x06, 0x0C, 0x0A, 0x2E, 0x34, 0x0F}},
        {0xE1, 15, {0x00, 0x0B, 0x11, 0x05, 0x13, 0x09, 0x33, 0x67, 0x48, 0x07, 0x0E, 0x0B, 0x2E, 0x33, 0x0F}},
        {0xB6, 4, {0x08, 0x82, 0x1D, 0x04}},
    };
    for (size_t i = 0; i < sizeof(seq) / sizeof(seq[0]); i++) esp_lcd_panel_io_tx_param(io, seq[i].cmd, seq[i].data, seq[i].len);
}

static lv_display_t *lcd_init(void)
{
    // The SD card shares the bus: keep it deselected.
    gpio_reset_pin(PIN_SD_CS);
    gpio_set_direction(PIN_SD_CS, GPIO_MODE_OUTPUT);
    gpio_set_level(PIN_SD_CS, 1);

    spi_bus_config_t bus = {
        .sclk_io_num = PIN_SPI_CLK,
        .mosi_io_num = PIN_SPI_MOSI,
        .miso_io_num = PIN_SPI_MISO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = LCD_W * 20 * 2 + 8,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI3_HOST, &bus, SPI_DMA_CH_AUTO));

    esp_lcd_panel_io_handle_t io;
    esp_lcd_panel_io_spi_config_t io_cfg = {
        .dc_gpio_num = PIN_LCD_DC,
        .cs_gpio_num = PIN_LCD_CS,
        .pclk_hz = 40 * 1000 * 1000,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .trans_queue_depth = 10,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI3_HOST, &io_cfg, &io));

    esp_lcd_panel_handle_t panel;
    esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = -1,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR,
        .bits_per_pixel = 16,
    };
    // The generic MIPI commands (sleep out, pixel format, address mode) are the same as the ST7789's.
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(io, &panel_cfg, &panel));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel));
    lcd_tune(io);
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel));
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel, true));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel, true));

    lvgl_port_cfg_t port = ESP_LVGL_PORT_INIT_CONFIG();
    port.task_stack = 8192;
    port.task_affinity = 0;
    port.task_stack_caps = MALLOC_CAP_SPIRAM;  // UI code reaches flash only through flash_run()
    ESP_ERROR_CHECK(lvgl_port_init(&port));
    lvgl_port_display_cfg_t disp = {
        .io_handle = io,
        .panel_handle = panel,
        .buffer_size = LCD_W * 20,  // DMA buffers must be internal RAM, which is tight on the ESP32
        .double_buffer = true,
        .hres = LCD_W,
        .vres = LCD_H,
        .color_format = LV_COLOR_FORMAT_RGB565,
        .flags = {.buff_dma = true, .swap_bytes = true},
    };
    return lvgl_port_add_disp(&disp);
}

bool board_touch_read(int *x, int *y)
{
    uint8_t reg = 0x02, d[5];
    bool touched = i2c_master_transmit_receive(s_touch, &reg, 1, d, sizeof(d), 50) == ESP_OK && (d[0] & 0x0F) > 0 &&
                   (d[0] & 0x0F) <= 2;
    int tx = (d[1] & 0x0F) << 8 | d[2], ty = (d[3] & 0x0F) << 8 | d[4];
    if (!touched || ty >= BUTTON_Y) {
        int b = touched ? (tx < LCD_W / 3 ? 0 : tx < LCD_W * 2 / 3 ? 1 : 2) : -1;
        if (b >= 0 && b != s_button) s_button_pressed = b;
        s_button = b;
        return false;
    }
    s_button = -1;
    *x = tx < LCD_W ? tx : LCD_W - 1;
    *y = ty;
    return true;
}

bool board_input_next(board_event_t *ev)
{
    // A: back, B: like the trackball click (opens the selected row, play/pause in the player), C: unused yet.
    static const board_input_t map[3] = {IN_BACK, IN_CLICK, IN_NONE};
    ev->ch = 0;
    if (s_button_pressed < 0) return false;
    ev->type = map[s_button_pressed];
    s_button_pressed = -1;
    return ev->type != IN_NONE;
}

void board_backlight(bool on) { axp_bits(AXP_LDO_ON, LDO_BLDO1, on); }

int board_battery_percent(void)
{
    if (!(axp_read(AXP_STATUS1) & 0x08)) return -1;  // no battery
    int pct = axp_read(AXP_BATTERY_PERCENT);
    return pct > 100 ? 100 : pct;
}

lv_display_t *board_init(void)
{
    i2c_init();
    power_init();
    lv_display_t *disp = lcd_init();
    board_backlight(true);
    ESP_LOGI(TAG, "Core2: battery %d %%", board_battery_percent());
    return disp;
}
