/* Version: 0.9 (Pre-Release) | JBG Discovery Framework */
#include "panel_hw.h"
#include "panel_hw_link.h"
#include "board_pins.h"
#include "identity.h"
#include "spi_presets.h"
#include "ui_colors.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_lcd_ili9341.h"
#include "esp_lcd_gc9a01.h"
#include "esp_lcd_ili9488.h"
#include "esp_lcd_panel_commands.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_ssd1306.h"
#include "esp_lcd_panel_st7789.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_st7735.h"
#include "esp_lcd_panel_sh1106.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "panel_hw";

typedef enum {
    PHW_NONE = 0,
    PHW_SPI,
    PHW_I2C,
} phw_kind_t;

static phw_kind_t s_kind;
static esp_lcd_panel_handle_t s_panel;
static esp_lcd_panel_io_handle_t s_io;
static i2c_master_bus_handle_t s_i2c_bus;
static bool s_spi_bus_ok;
static uint16_t s_w;
static uint16_t s_h;
static uint8_t s_bpp;
static uint32_t s_pclk_hz;
static session_spi_chip_t s_spi_chip;
/* Controller GRAM preset max (chip geometry); nuclear clear floods this, not logical s_w/s_h only. */
static uint16_t s_phys_w;
static uint16_t s_phys_h;
/* Mirrored from last successful esp_lcd_panel_set_gap (SPI address window uses these). */
static int16_t s_gap_col;
static int16_t s_gap_row;
/* I2C: logical RGB565 compositing buffer; flushed to 1 bpp before esp_lcd_panel_draw_bitmap. */
static uint16_t *s_i2c_shadow;
static bool s_bl_ledc_ready;
static bool s_bl_timer_inited;

/* Forward declarations — MADCTL snapshot helpers (defined with panel_hw_apply_orientation). */
static uint8_t panel_hw_effective_user_madctl_byte(const test_session_t *s);
static void panel_hw_session_write_madctl_snapshot(test_session_t *s);

#define LEDC_BLK_TIMER   LEDC_TIMER_0
#define LEDC_BLK_MODE    LEDC_LOW_SPEED_MODE
#define LEDC_BLK_CHANNEL LEDC_CHANNEL_0
#define LEDC_BLK_RES     LEDC_TIMER_13_BIT

/* Backlight low as GPIO before LEDC init; required after deinit or LEDC will not drive the pin. */
static void backlight_pin_drive_low_pre_bringup(void)
{
    gpio_reset_pin(BOARD_DISPLAY_SPI_BL);
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << (unsigned)BOARD_DISPLAY_SPI_BL,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    (void)gpio_config(&io);
    (void)gpio_set_level(BOARD_DISPLAY_SPI_BL, 0);
}

/* ≥10 ms low before panel driver takes RST; GRAM may still hold junk — software wipe remains required. */
static void display_spi_rst_assert_min_10ms(void)
{
    if (BOARD_DISPLAY_SPI_RST < 0) {
        return;
    }
    gpio_reset_pin(BOARD_DISPLAY_SPI_RST);
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << (unsigned)BOARD_DISPLAY_SPI_RST,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    (void)gpio_config(&io);
    (void)gpio_set_level(BOARD_DISPLAY_SPI_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(12));
    (void)gpio_set_level(BOARD_DISPLAY_SPI_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(2));
}

static void backlight_ledc_init(void)
{
    gpio_reset_pin(BOARD_DISPLAY_SPI_BL);

    if (!s_bl_timer_inited) {
        ledc_timer_config_t t = {
            .speed_mode = LEDC_BLK_MODE,
            .timer_num = LEDC_BLK_TIMER,
            .duty_resolution = LEDC_BLK_RES,
            .freq_hz = 5000,
            .clk_cfg = LEDC_AUTO_CLK,
        };
        ESP_ERROR_CHECK(ledc_timer_config(&t));
        s_bl_timer_inited = true;
    }

    ledc_channel_config_t ch = {
        .speed_mode = LEDC_BLK_MODE,
        .channel = LEDC_BLK_CHANNEL,
        .timer_sel = LEDC_BLK_TIMER,
        .intr_type = LEDC_INTR_DISABLE,
        .gpio_num = BOARD_DISPLAY_SPI_BL,
        .duty = 0,
        .hpoint = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ch));
    s_bl_ledc_ready = true;
}

static void spi_del_panel_and_io(void)
{
    if (s_panel) {
        esp_lcd_panel_del(s_panel);
        s_panel = NULL;
    }
    if (s_io) {
        esp_lcd_panel_io_del(s_io);
        s_io = NULL;
    }
}

void panel_hw_deinit(void)
{
    if (s_i2c_shadow) {
        heap_caps_free(s_i2c_shadow);
        s_i2c_shadow = NULL;
    }
    spi_del_panel_and_io();
    if (s_spi_bus_ok) {
        spi_bus_free(SPI2_HOST);
        s_spi_bus_ok = false;
    }
    if (s_i2c_bus) {
        i2c_del_master_bus(s_i2c_bus);
        s_i2c_bus = NULL;
    }
    s_kind = PHW_NONE;
    s_w = 0;
    s_h = 0;
    s_bpp = 0;
    s_pclk_hz = 0;
    s_spi_chip = SESSION_SPI_CHIP_NONE;
    s_phys_w = 0;
    s_phys_h = 0;
    s_gap_col = 0;
    s_gap_row = 0;
    if (s_bl_ledc_ready) {
        ledc_set_duty(LEDC_BLK_MODE, LEDC_BLK_CHANNEL, 0);
        ledc_update_duty(LEDC_BLK_MODE, LEDC_BLK_CHANNEL);
    }
    s_bl_ledc_ready = false;
}

static void gpio_display_pin_hiz(gpio_num_t pin)
{
    if (pin < 0) {
        return;
    }
    gpio_reset_pin(pin);
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << (unsigned)pin,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    (void)gpio_config(&io);
}

static void gpio_backlight_force_off(void)
{
    (void)ledc_stop(LEDC_BLK_MODE, LEDC_BLK_CHANNEL, 0);
    gpio_reset_pin(BOARD_DISPLAY_SPI_BL);
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << (unsigned)BOARD_DISPLAY_SPI_BL,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    (void)gpio_config(&io);
    (void)gpio_set_level(BOARD_DISPLAY_SPI_BL, 0);
}

void panel_hw_safe_swap_pause(void)
{
    panel_hw_deinit();
    gpio_display_pin_hiz(BOARD_DISPLAY_SPI_MOSI);
    gpio_display_pin_hiz(BOARD_DISPLAY_SPI_SCK);
    gpio_display_pin_hiz(BOARD_DISPLAY_SPI_CS);
    gpio_display_pin_hiz(BOARD_DISPLAY_SPI_DC);
    gpio_display_pin_hiz(BOARD_DISPLAY_SPI_RST);
    gpio_display_pin_hiz((gpio_num_t)BOARD_DISPLAY_SPI_MISO);
    gpio_display_pin_hiz(BOARD_DISPLAY_I2C_SDA);
    gpio_display_pin_hiz(BOARD_DISPLAY_I2C_SCL);
    gpio_backlight_force_off();
}

esp_err_t panel_hw_reinit_from_session(test_session_t *s)
{
    ESP_RETURN_ON_FALSE(s && s->panel_ready, ESP_ERR_INVALID_STATE, TAG, "session");

    esp_err_t err = ESP_FAIL;
    if (s->bus == SESSION_BUS_SPI && s->spi_chip != SESSION_SPI_CHIP_NONE) {
        uint32_t pc = s->spi_pclk_hz;
        if (pc == 0 || pc > 40 * 1000 * 1000) {
            pc = 20 * 1000 * 1000;
        }
        err = panel_hw_spi_init(s, s->spi_chip, s->hor_res, s->ver_res, pc);
    } else if (s->bus == SESSION_BUS_I2C && s->i2c_driver != SESSION_I2C_DRV_NONE) {
        err = panel_hw_i2c_init(s, s->i2c_driver, s->i2c_addr_7bit, s->ssd1306_height);
    } else {
        return ESP_ERR_NOT_SUPPORTED;
    }

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "reinit_from_session: %s", esp_err_to_name(err));
        session_reset_display_fields(s);
        return err;
    }

    panel_hw_apply_gap(s);
    panel_hw_apply_orientation(s);
    panel_hw_apply_invert(s);
    panel_hw_set_backlight_pct(s);
    if (panel_hw_is_spi()) {
        (void)panel_hw_fill_rgb565(ui_color_bg(s));
    } else if (panel_hw_is_i2c()) {
        (void)panel_hw_fill_mono(0x00);
    }
    return ESP_OK;
}

bool panel_hw_panel_ready(void)
{
    return s_panel != NULL;
}

bool panel_hw_is_spi(void)
{
    return s_kind == PHW_SPI;
}

bool panel_hw_is_i2c(void)
{
    return s_kind == PHW_I2C;
}

uint32_t panel_hw_spi_pclk_hz(void)
{
    return s_pclk_hz;
}

uint8_t panel_hw_bits_per_pixel(void)
{
    return s_bpp;
}

void panel_hw_spi_fb_size(uint16_t *out_w, uint16_t *out_h)
{
    if (out_w) {
        *out_w = (s_kind == PHW_SPI) ? s_w : 0;
    }
    if (out_h) {
        *out_h = (s_kind == PHW_SPI) ? s_h : 0;
    }
}

static void panel_hw_spi_refresh_phys_dims(void)
{
    if (s_kind == PHW_SPI && s_spi_chip != SESSION_SPI_CHIP_NONE) {
        spi_presets_chip_gram_max(s_spi_chip, &s_phys_w, &s_phys_h);
    } else {
        s_phys_w = 0;
        s_phys_h = 0;
    }
}

/* DCS CASET/RASET inclusive, chip memory space (matches esp_lcd ST77xx draw_bitmap layout). */
static esp_err_t spi_dcaset_raset_inclusive(int xs, int ys, int xe_inc, int ye_inc)
{
    if (!s_io || s_kind != PHW_SPI || s_bpp != 16) {
        return ESP_OK;
    }
    if (xe_inc < xs || ye_inc < ys) {
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t caset[4] = {
        (uint8_t)((xs >> 8) & 0xFF),
        (uint8_t)(xs & 0xFF),
        (uint8_t)((xe_inc >> 8) & 0xFF),
        (uint8_t)(xe_inc & 0xFF),
    };
    uint8_t raset[4] = {
        (uint8_t)((ys >> 8) & 0xFF),
        (uint8_t)(ys & 0xFF),
        (uint8_t)((ye_inc >> 8) & 0xFF),
        (uint8_t)(ye_inc & 0xFF),
    };
    esp_err_t e = esp_lcd_panel_io_tx_param(s_io, LCD_CMD_CASET, caset, sizeof(caset));
    if (e != ESP_OK) {
        return e;
    }
    return esp_lcd_panel_io_tx_param(s_io, LCD_CMD_RASET, raset, sizeof(raset));
}

static esp_err_t spi_address_window_full_panel(void)
{
    if (!s_io || s_kind != PHW_SPI || s_bpp != 16 || s_w == 0 || s_h == 0) {
        return ESP_OK;
    }
    int xs = (int)s_gap_col;
    int ys = (int)s_gap_row;
    int xe = xs + (int)s_w - 1;
    int ye = ys + (int)s_h - 1;
    return spi_dcaset_raset_inclusive(xs, ys, xe, ye);
}

static esp_err_t spi_address_window_native_mn(int mw, int mh)
{
    if (mw <= 0 || mh <= 0) {
        return ESP_ERR_INVALID_SIZE;
    }
    return spi_dcaset_raset_inclusive(0, 0, mw - 1, mh - 1);
}

/* Every RGB565 draw: full window before + full window after (no stale CASET/RASET). */
static esp_err_t spi16_panel_draw_bitmap_atomic(int x0, int y0, int x1, int y1, const void *pixels)
{
    if (!s_panel || s_kind != PHW_SPI || s_bpp != 16 || !pixels) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t e = spi_address_window_full_panel();
    if (e != ESP_OK) {
        return e;
    }
    e = esp_lcd_panel_draw_bitmap(s_panel, x0, y0, x1, y1, pixels);
    esp_err_t e2 = spi_address_window_full_panel();
    return (e != ESP_OK) ? e : e2;
}

void panel_hw_set_backlight_pct(const test_session_t *s)
{
    if (s_kind != PHW_SPI || !s_bl_ledc_ready) {
        return;
    }
    uint8_t p = s->backlight_pct;
    if (p > 100) {
        p = 100;
    }
    uint32_t max_d = (1U << LEDC_BLK_RES) - 1;
    uint32_t duty = (max_d * (uint32_t)p) / 100U;
    ledc_set_duty(LEDC_BLK_MODE, LEDC_BLK_CHANNEL, duty);
    ledc_update_duty(LEDC_BLK_MODE, LEDC_BLK_CHANNEL);
}

static esp_err_t spi_create_io(uint32_t pclk_hz)
{
    esp_lcd_panel_io_spi_config_t iocfg = {
        .cs_gpio_num = BOARD_DISPLAY_SPI_CS,
        .dc_gpio_num = BOARD_DISPLAY_SPI_DC,
        .spi_mode = 0,
        .pclk_hz = pclk_hz,
        .trans_queue_depth = 10,
        .on_color_trans_done = NULL,
        .user_ctx = NULL,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    return esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST, &iocfg, &s_io);
}

static const char *spi_chip_short_name(session_spi_chip_t chip)
{
    switch (chip) {
    case SESSION_SPI_ST7735:
        return "ST7735";
    case SESSION_SPI_ST7789:
        return "ST7789";
    case SESSION_SPI_ILI9341:
        return "ILI9341";
    case SESSION_SPI_ILI9488:
        return "ILI9488";
    case SESSION_SPI_GC9A01:
        return "GC9A01";
    case SESSION_SPI_ST7796:
        return "ST7796";
    default:
        return "?";
    }
}

static esp_err_t spi_create_panel_for_chip(test_session_t *s, session_spi_chip_t chip)
{
    /* Reduce esp_lcd driver log noise on UART during interactive serial UI. */
    esp_log_level_set("st7735", ESP_LOG_WARN);
    esp_log_level_set("lcd_panel", ESP_LOG_WARN);

    display_spi_rst_assert_min_10ms();

    esp_lcd_panel_dev_config_t pc = {
        .reset_gpio_num = BOARD_DISPLAY_SPI_RST,
        .rgb_ele_order =
            s->spi_rgb_ele_order_rgb ? LCD_RGB_ELEMENT_ORDER_RGB : LCD_RGB_ELEMENT_ORDER_BGR,
        .bits_per_pixel = 16,
    };

    esp_err_t err = ESP_OK;

    switch (chip) {
    case SESSION_SPI_ST7735:
        err = esp_lcd_new_panel_st7735(s_io, &pc, &s_panel);
        break;
    case SESSION_SPI_ST7789:
        err = esp_lcd_new_panel_st7789(s_io, &pc, &s_panel);
        break;
    case SESSION_SPI_ILI9341:
        err = esp_lcd_new_panel_ili9341(s_io, &pc, &s_panel);
        break;
    case SESSION_SPI_GC9A01:
        err = esp_lcd_new_panel_gc9a01(s_io, &pc, &s_panel);
        break;
    case SESSION_SPI_ILI9488: {
        pc.bits_per_pixel = 18;
        size_t buf_sz = (size_t)s_w * 24 * 3;
        if (buf_sz < 1024) {
            buf_sz = 1024;
        }
        err = esp_lcd_new_panel_ili9488(s_io, &pc, buf_sz, &s_panel);
        break;
    }
    case SESSION_SPI_ST7796:
        /* ST7796: add vendor esp_lcd factory when available. */
        err = ESP_ERR_NOT_SUPPORTED;
        break;
    default:
        return ESP_ERR_NOT_SUPPORTED;
    }

    if (err != ESP_OK) {
        return err;
    }

    err = esp_lcd_panel_reset(s_panel);
    if (err != ESP_OK) {
        goto fail;
    }
    err = esp_lcd_panel_init(s_panel);
    if (err != ESP_OK) {
        goto fail;
    }
    err = esp_lcd_panel_disp_on_off(s_panel, true);
    if (err != ESP_OK) {
        goto fail;
    }

    /* Post sleep-out / display-on delay before first large GRAM write. */
    vTaskDelay(pdMS_TO_TICKS(150));

    /* First-light scrub: first full GRAM write before gap/viewport and before backlight (caller holds BL low). */
    if (s_bpp == 16) {
        panel_hw_spi_wipe_controller_gram_rgb565(s, ui_color_bg(s));
    }

    return ESP_OK;

fail:
    ESP_LOGE(TAG, "panel post-init failed: %s", esp_err_to_name(err));
    esp_lcd_panel_del(s_panel);
    s_panel = NULL;
    return err;
}

esp_err_t panel_hw_spi_init(test_session_t *s, session_spi_chip_t chip, uint16_t w, uint16_t h, uint32_t pclk_hz)
{
    ESP_RETURN_ON_FALSE(s && chip != SESSION_SPI_CHIP_NONE, ESP_ERR_INVALID_ARG, TAG, "args");
    ESP_RETURN_ON_FALSE(w > 0 && w <= 480 && h > 0 && h <= 800, ESP_ERR_INVALID_ARG, TAG, "geometry");

    /* Default invert path when chip family changes; same-chip re-inits preserve session colour map. */
    session_spi_chip_t prev_chip = s->spi_chip;

    panel_hw_deinit();
    backlight_pin_drive_low_pre_bringup();
    vTaskDelay(pdMS_TO_TICKS(50));
    display_spi_rst_assert_min_10ms();

    size_t max_tx = (size_t)w * (size_t)h * 3 + 256;
    if (max_tx < 4096) {
        max_tx = 4096;
    }

    spi_bus_config_t buscfg = {
        .mosi_io_num = BOARD_DISPLAY_SPI_MOSI,
        .miso_io_num = BOARD_DISPLAY_SPI_MISO,
        .sclk_io_num = BOARD_DISPLAY_SPI_SCK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = (int)max_tx,
    };

    esp_err_t err = spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "spi_bus_initialize: %s", esp_err_to_name(err));
        return err;
    }
    s_spi_bus_ok = true;
    s_w = w;
    s_h = h;
    s_spi_chip = chip;
    s_pclk_hz = pclk_hz;
    s_bpp = (chip == SESSION_SPI_ILI9488) ? 18 : 16;
    s_kind = PHW_SPI;

    err = spi_create_io(pclk_hz);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "panel_io: %s", esp_err_to_name(err));
        panel_hw_deinit();
        return err;
    }

    if (chip == SESSION_SPI_ST7735 || chip == SESSION_SPI_GC9A01) {
        if (prev_chip != chip) {
            s->inv_en = true;
        }
    } else if (prev_chip != chip) {
        s->inv_en = false;
    }

    err = spi_create_panel_for_chip(s, chip);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "panel: %s", esp_err_to_name(err));
        panel_hw_deinit();
        return err;
    }

    /* RGB565: spi_create_panel_for_chip ran GRAM wipe + session gap/orient/invert inside wipe. */
    if (s_bpp != 16) {
        panel_hw_apply_gap(s);
        panel_hw_apply_orientation(s);
        panel_hw_apply_invert(s);
    }

    vTaskDelay(pdMS_TO_TICKS(100));

    backlight_ledc_init();
    if (s->backlight_pct == 0) {
        s->backlight_pct = 75;
    }
    panel_hw_set_backlight_pct(s);

    s->spi_chip = chip;
    s->hor_res = w;
    s->ver_res = h;
    s->spi_pclk_hz = pclk_hz;
    s->panel_ready = true;

    snprintf(s->profile_tag, sizeof(s->profile_tag), "%s %ux%u", spi_chip_short_name(chip), (unsigned)w,
             (unsigned)h);

    panel_hw_spi_refresh_phys_dims();
    panel_hw_session_write_madctl_snapshot(s);
    return ESP_OK;
}

esp_err_t panel_hw_spi_set_pclk(test_session_t *s, uint32_t pclk_hz)
{
    ESP_RETURN_ON_FALSE(s && s_spi_bus_ok && s_kind == PHW_SPI, ESP_ERR_INVALID_STATE, TAG, "spi");
    session_spi_chip_t chip = s_spi_chip;

    spi_del_panel_and_io();

    s_pclk_hz = pclk_hz;
    ESP_RETURN_ON_ERROR(spi_create_io(pclk_hz), TAG, "io");
    ESP_RETURN_ON_ERROR(spi_create_panel_for_chip(s, chip), TAG, "panel");
    s->spi_pclk_hz = pclk_hz;
    panel_hw_apply_gap(s);
    panel_hw_apply_orientation(s);
    panel_hw_apply_invert(s);
    if (s_bpp == 16) {
        panel_hw_spi_wipe_controller_gram_rgb565(s, ui_color_bg(s));
    }
    panel_hw_set_backlight_pct(s);
    panel_hw_spi_refresh_phys_dims();
    panel_hw_session_write_madctl_snapshot(s);
    return ESP_OK;
}

static esp_err_t i2c_flush_shadow_to_panel(void);

esp_err_t panel_hw_i2c_init(test_session_t *s, session_i2c_driver_t drv, uint8_t addr_7bit, uint8_t ssd1306_height)
{
    ESP_RETURN_ON_FALSE(s && addr_7bit >= 0x08 && addr_7bit <= 0x77, ESP_ERR_INVALID_ARG, TAG, "i2c args");

    panel_hw_deinit();

    i2c_master_bus_config_t bus_cfg;
    display_i2c_bus_config(&bus_cfg);
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_cfg, &s_i2c_bus), TAG, "i2c bus");

    esp_lcd_panel_io_i2c_config_t iocfg = {
        .dev_addr = addr_7bit,
        .on_color_trans_done = NULL,
        .user_ctx = NULL,
        .control_phase_bytes = 1,
        .dc_bit_offset = 6,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .flags = { .dc_low_on_data = false, .disable_control_phase = false },
        .scl_speed_hz = 400 * 1000,
    };

    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_i2c(s_i2c_bus, &iocfg, &s_io), TAG, "panel io");

    esp_lcd_panel_dev_config_t pc = {
        .bits_per_pixel = 1,
        .reset_gpio_num = -1,
    };

    esp_err_t err = ESP_OK;
    if (drv == SESSION_I2C_DRV_SSD1306) {
        if (ssd1306_height != 32 && ssd1306_height != 64) {
            ssd1306_height = 64;
        }
        esp_lcd_panel_ssd1306_config_t ssd = { .height = ssd1306_height };
        pc.vendor_config = &ssd;
        err = esp_lcd_new_panel_ssd1306(s_io, &pc, &s_panel);
        s_h = ssd1306_height;
        snprintf(s->profile_tag, sizeof(s->profile_tag), "SSD1306 128x%u 0x%02X", (unsigned)ssd1306_height, addr_7bit);
    } else if (drv == SESSION_I2C_DRV_SH1106) {
        pc.vendor_config = NULL;
        err = esp_lcd_new_panel_sh1106(s_io, &pc, &s_panel);
        s_h = 64;
        snprintf(s->profile_tag, sizeof(s->profile_tag), "SH1106 128x64 0x%02X", addr_7bit);
    } else {
        panel_hw_deinit();
        return ESP_ERR_NOT_SUPPORTED;
    }

    if (err != ESP_OK) {
        panel_hw_deinit();
        return err;
    }

    s_w = 128;
    s_bpp = 1;
    s_kind = PHW_I2C;
    s_pclk_hz = 0;

    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(s_panel), TAG, "rst");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(s_panel), TAG, "init");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(s_panel, true), TAG, "on");

    if (s_i2c_shadow) {
        heap_caps_free(s_i2c_shadow);
        s_i2c_shadow = NULL;
    }
    s_i2c_shadow = (uint16_t *)heap_caps_calloc((size_t)s_w * (size_t)s_h, sizeof(uint16_t), MALLOC_CAP_INTERNAL);
    if (!s_i2c_shadow) {
        panel_hw_deinit();
        return ESP_ERR_NO_MEM;
    }

    s->i2c_driver = drv;
    s->ssd1306_height = (drv == SESSION_I2C_DRV_SSD1306) ? ssd1306_height : 64;
    s->hor_res = s_w;
    s->ver_res = s_h;
    s->panel_ready = true;
    s->spi_chip = SESSION_SPI_CHIP_NONE;
    s->inv_en = false;

    panel_hw_apply_gap(s);
    panel_hw_apply_orientation(s);
    panel_hw_apply_invert(s);
    panel_hw_session_write_madctl_snapshot(s);

    (void)i2c_flush_shadow_to_panel();
    return ESP_OK;
}

esp_err_t panel_hw_set_gap(int16_t col, int16_t row)
{
    if (!s_panel) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t e = esp_lcd_panel_set_gap(s_panel, (int)col, (int)row);
    if (e == ESP_OK) {
        s_gap_col = col;
        s_gap_row = row;
    }
    return e;
}

void panel_hw_set_mirror(bool mirror_x, bool mirror_y)
{
    if (!s_panel) {
        return;
    }
    (void)esp_lcd_panel_mirror(s_panel, mirror_x, mirror_y);
}

void panel_hw_apply_gap(const test_session_t *s)
{
    if (!s || !s_panel) {
        return;
    }
    (void)panel_hw_set_gap(s->gap_col, s->gap_row);
}

void panel_hw_apply_orientation(const test_session_t *s)
{
    if (!s_panel) {
        return;
    }
    static const bool sw[4] = { false, true, false, true };
    static const bool bx[4] = { false, true, true, false };
    static const bool by[4] = { false, false, true, true };
    uint8_t r = s->rot_quarter & 3;
    (void)esp_lcd_panel_swap_xy(s_panel, sw[r]);
    panel_hw_set_mirror(bx[r] ^ s->mirror_x, by[r] ^ s->mirror_y);
}

/* Same MV/MX/MY mapping as esp_lcd ST77xx swap_xy + mirror; RGB/BGR bit from session. */
static uint8_t panel_hw_effective_user_madctl_byte(const test_session_t *s)
{
    if (!s || s->bus != SESSION_BUS_SPI || panel_hw_bits_per_pixel() != 16) {
        return 0u;
    }
    static const bool sw[4] = { false, true, false, true };
    static const bool bx[4] = { false, true, true, false };
    static const bool by[4] = { false, false, true, true };
    uint8_t r = s->rot_quarter & 3;
    bool mv = sw[r];
    bool mx = bx[r] ^ s->mirror_x;
    bool my = by[r] ^ s->mirror_y;
    uint8_t m = 0;
    if (mv) {
        m |= LCD_CMD_MV_BIT;
    }
    if (mx) {
        m |= LCD_CMD_MX_BIT;
    }
    if (my) {
        m |= LCD_CMD_MY_BIT;
    }
    if (s->spi_rgb_ele_order_rgb) {
        m |= 0x08u;
    }
    return m;
}

static void panel_hw_session_write_madctl_snapshot(test_session_t *s)
{
    if (!s) {
        return;
    }
    s->madctl = panel_hw_effective_user_madctl_byte(s);
}

void panel_hw_set_orientation(test_session_t *s)
{
    if (!s) {
        return;
    }
    panel_hw_apply_orientation(s);
    panel_hw_session_write_madctl_snapshot(s);
}

void panel_hw_apply_invert(const test_session_t *s)
{
    if (!s_panel) {
        return;
    }
    (void)esp_lcd_panel_invert_color(s_panel, s->inv_en);
}

void panel_hw_set_inversion(test_session_t *s)
{
    if (!s) {
        return;
    }
    panel_hw_apply_invert(s);
    panel_hw_session_write_madctl_snapshot(s);
}

void panel_hw_spi_clear_hardware_gap(void)
{
    if (!s_panel || s_kind != PHW_SPI) {
        return;
    }
    (void)panel_hw_set_gap(0, 0);
}

/* ST7735/ST7789/ILI9341-class MADCTL: MX column order bit 6, MY row order bit 7 (socket default). */
#define PANEL_HW_DRIVER_MADCTL_MX 0x40u
#define PANEL_HW_DRIVER_MADCTL_MY 0x80u

static uint8_t panel_hw_map_madctl(panel_mirror_t m)
{
    switch (m) {
    case PANEL_MIRROR_XY:
        return (uint8_t)(PANEL_HW_DRIVER_MADCTL_MX | PANEL_HW_DRIVER_MADCTL_MY);
    case PANEL_MIRROR_X:
        return PANEL_HW_DRIVER_MADCTL_MX;
    case PANEL_MIRROR_Y:
        return PANEL_HW_DRIVER_MADCTL_MY;
    case PANEL_MIRROR_NONE:
    default:
        return 0;
    }
}

void panel_hw_session_set_silicon_mirror(test_session_t *s, panel_mirror_t m)
{
    if (!s) {
        return;
    }
    s->silicon_mirror = m;
}

static uint8_t spi_silicon_madctl_byte(const test_session_t *s, uint8_t mirror_bits_mxmy)
{
    uint8_t m = (uint8_t)(mirror_bits_mxmy & (PANEL_HW_DRIVER_MADCTL_MX | PANEL_HW_DRIVER_MADCTL_MY));
    if (s->spi_rgb_ele_order_rgb) {
        m |= 0x08u;
    }
    return m;
}

uint8_t panel_hw_truth_madctl_byte(const test_session_t *s)
{
    if (!s || s->bus != SESSION_BUS_SPI) {
        return 0u;
    }
    uint8_t mxmy = panel_hw_map_madctl(s->silicon_mirror);
    return spi_silicon_madctl_byte(s, mxmy);
}

void panel_hw_set_silicon_basis(const test_session_t *s)
{
    if (!s || !s_io || !s_panel || s_kind != PHW_SPI || s_bpp != 16) {
        return;
    }

    panel_hw_spi_clear_hardware_gap();
    (void)esp_lcd_panel_swap_xy(s_panel, false);

    uint8_t mxmy = panel_hw_map_madctl(s->silicon_mirror);
    uint8_t mad = spi_silicon_madctl_byte(s, mxmy);
    esp_err_t err = esp_lcd_panel_io_tx_param(s_io, LCD_CMD_MADCTL, &mad, 1);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "set_silicon_basis MADCTL: %s", esp_err_to_name(err));
    }

    bool mx = (mxmy & PANEL_HW_DRIVER_MADCTL_MX) != 0;
    bool my = (mxmy & PANEL_HW_DRIVER_MADCTL_MY) != 0;
    panel_hw_set_mirror(mx, my);
}

static inline bool rgb565_pixel_lit(uint16_t p)
{
    return p != 0u;
}

/* SSD1306 esp_lcd: row-major, MSB = left pixel in each horizontal byte (see esp_lcd_panel_ssd1306 draw_bitmap). */
static esp_err_t i2c_flush_shadow_to_panel(void)
{
    ESP_RETURN_ON_FALSE(s_panel && s_kind == PHW_I2C && s_i2c_shadow, ESP_ERR_INVALID_STATE, TAG, "i2c shadow");
    ESP_RETURN_ON_FALSE(((size_t)s_w * (size_t)s_h) % 8u == 0, ESP_ERR_INVALID_SIZE, TAG, "i2c WxH");

    const size_t nbytes = (size_t)s_w * (size_t)s_h / 8u;
    uint8_t *mono = heap_caps_malloc(nbytes, MALLOC_CAP_DMA);
    ESP_RETURN_ON_FALSE(mono, ESP_ERR_NO_MEM, TAG, "i2c mono");

    size_t out = 0;
    for (int y = 0; y < (int)s_h; y++) {
        for (int bx = 0; bx < (int)s_w; bx += 8) {
            uint8_t b = 0;
            for (int bit = 0; bit < 8; bit++) {
                uint16_t p = s_i2c_shadow[(size_t)y * s_w + (size_t)(bx + bit)];
                if (rgb565_pixel_lit(p)) {
                    b |= (uint8_t)(0x80 >> bit);
                }
            }
            mono[out++] = b;
        }
    }

    esp_err_t e = esp_lcd_panel_draw_bitmap(s_panel, 0, 0, s_w, s_h, mono);
    free(mono);
    return e;
}

esp_err_t panel_hw_fill_rgb565(uint16_t rgb565)
{
    ESP_RETURN_ON_FALSE(s_panel, ESP_ERR_INVALID_STATE, TAG, "panel");

    if (s_kind == PHW_I2C) {
        ESP_RETURN_ON_FALSE(s_i2c_shadow, ESP_ERR_INVALID_STATE, TAG, "i2c shadow");
        const size_t n = (size_t)s_w * (size_t)s_h;
        for (size_t i = 0; i < n; i++) {
            s_i2c_shadow[i] = rgb565;
        }
        return i2c_flush_shadow_to_panel();
    }

    ESP_RETURN_ON_FALSE(s_kind == PHW_SPI, ESP_ERR_INVALID_STATE, TAG, "spi");

    const int stripe = 16;
    size_t max_pix = (size_t)s_w * stripe;
    size_t bytes = max_pix * sizeof(uint16_t);
    uint16_t *buf = heap_caps_malloc(bytes, MALLOC_CAP_DMA);
    ESP_RETURN_ON_FALSE(buf, ESP_ERR_NO_MEM, TAG, "buf");

    for (int y = 0; y < s_h;) {
        int y2 = y + stripe;
        if (y2 > s_h) {
            y2 = s_h;
        }
        int rows = y2 - y;
        size_t npix = (size_t)s_w * rows;
        for (size_t i = 0; i < npix; i++) {
            buf[i] = rgb565;
        }
        esp_err_t e;
        if (s_bpp == 16) {
            e = spi16_panel_draw_bitmap_atomic(0, y, s_w, y2, buf);
        } else {
            e = esp_lcd_panel_draw_bitmap(s_panel, 0, y, s_w, y2, buf);
        }
        if (e != ESP_OK) {
            free(buf);
            return e;
        }
        y = y2;
    }
    free(buf);
    return ESP_OK;
}

/* --- SPI RGB565 sub-rectangle helpers (stripe fill, small line buffers; no full-frame alloc) --- */

static void spi_clip_rect_to_panel(int *x, int *y, int *w, int *h)
{
    if (!w || !h || *w <= 0 || *h <= 0) {
        if (w) {
            *w = 0;
        }
        if (h) {
            *h = 0;
        }
        return;
    }
    int pw = (int)s_w;
    int ph = (int)s_h;
    if (*x < 0) {
        *w += *x;
        *x = 0;
    }
    if (*y < 0) {
        *h += *y;
        *y = 0;
    }
    if (*x >= pw || *y >= ph) {
        *w = 0;
        *h = 0;
        return;
    }
    if (*x + *w > pw) {
        *w = pw - *x;
    }
    if (*y + *h > ph) {
        *h = ph - *y;
    }
    if (*w < 0) {
        *w = 0;
    }
    if (*h < 0) {
        *h = 0;
    }
}

static void spi_clip_rect_to_bounds(int *x, int *y, int *w, int *h, int pw, int ph)
{
    if (!w || !h || *w <= 0 || *h <= 0 || pw <= 0 || ph <= 0) {
        if (w) {
            *w = 0;
        }
        if (h) {
            *h = 0;
        }
        return;
    }
    if (*x < 0) {
        *w += *x;
        *x = 0;
    }
    if (*y < 0) {
        *h += *y;
        *y = 0;
    }
    if (*x >= pw || *y >= ph) {
        *w = 0;
        *h = 0;
        return;
    }
    if (*x + *w > pw) {
        *w = pw - *x;
    }
    if (*y + *h > ph) {
        *h = ph - *y;
    }
    if (*w < 0) {
        *w = 0;
    }
    if (*h < 0) {
        *h = 0;
    }
}

static esp_err_t spi_hline_rgb565(int x, int y, int len, uint16_t c)
{
    if (len <= 0) {
        return ESP_OK;
    }
    int w = len;
    int h = 1;
    spi_clip_rect_to_panel(&x, &y, &w, &h);
    if (w <= 0 || h <= 0) {
        return ESP_OK;
    }

    size_t bytes = (size_t)w * sizeof(uint16_t);
    uint16_t *buf = heap_caps_malloc(bytes, MALLOC_CAP_DMA);
    ESP_RETURN_ON_FALSE(buf, ESP_ERR_NO_MEM, TAG, "hline buf");
    for (int i = 0; i < w; i++) {
        buf[i] = c;
    }
    esp_err_t e = (s_bpp == 16) ? spi16_panel_draw_bitmap_atomic(x, y, x + w, y + 1, buf)
                                : esp_lcd_panel_draw_bitmap(s_panel, x, y, x + w, y + 1, buf);
    free(buf);
    return e;
}

static esp_err_t spi_vline_rgb565(int x, int y, int len, uint16_t c)
{
    if (len <= 0) {
        return ESP_OK;
    }
    const int chunk = 32;
    uint16_t *buf = heap_caps_malloc((size_t)chunk * sizeof(uint16_t), MALLOC_CAP_DMA);
    ESP_RETURN_ON_FALSE(buf, ESP_ERR_NO_MEM, TAG, "vline buf");
    for (int i = 0; i < chunk; i++) {
        buf[i] = c;
    }
    esp_err_t err = ESP_OK;
    for (int rem = len, yy = y; rem > 0 && err == ESP_OK;) {
        int n = rem;
        if (n > chunk) {
            n = chunk;
        }
        int xc = x;
        int yc = yy;
        int wc = 1;
        int hc = n;
        spi_clip_rect_to_panel(&xc, &yc, &wc, &hc);
        if (wc > 0 && hc > 0) {
            err = (s_bpp == 16) ? spi16_panel_draw_bitmap_atomic(xc, yc, xc + wc, yc + hc, buf)
                               : esp_lcd_panel_draw_bitmap(s_panel, xc, yc, xc + wc, yc + hc, buf);
        }
        yy += n;
        rem -= n;
    }
    free(buf);
    return err;
}

static esp_err_t spi_fill_rect_rgb565(int x, int y, int w, int h, uint16_t c)
{
    spi_clip_rect_to_panel(&x, &y, &w, &h);
    if (w <= 0 || h <= 0) {
        return ESP_OK;
    }

    const int x0 = x;
    const int y0 = y;
    const int y1 = y0 + h;

    const int stripe = 16;
    size_t max_pix = (size_t)s_w * (size_t)stripe;
    if (max_pix < (size_t)w) {
        max_pix = (size_t)w;
    }
    size_t bytes = max_pix * sizeof(uint16_t);
    uint16_t *buf = heap_caps_malloc(bytes, MALLOC_CAP_DMA);
    ESP_RETURN_ON_FALSE(buf, ESP_ERR_NO_MEM, TAG, "rect fill buf");

    esp_err_t err = ESP_OK;
    for (int yy = y0; yy < y1 && err == ESP_OK;) {
        int y2 = yy + stripe;
        if (y2 > y1) {
            y2 = y1;
        }
        int rows = y2 - yy;
        int rw = w;
        int rx = x0;
        int ry = yy;
        spi_clip_rect_to_panel(&rx, &ry, &rw, &rows);
        if (rw <= 0 || rows <= 0) {
            yy = y2;
            continue;
        }
        size_t npix = (size_t)rw * (size_t)rows;
        for (size_t i = 0; i < npix; i++) {
            buf[i] = c;
        }
        err = (s_bpp == 16) ? spi16_panel_draw_bitmap_atomic(rx, ry, rx + rw, ry + rows, buf)
                            : esp_lcd_panel_draw_bitmap(s_panel, rx, ry, rx + rw, ry + rows, buf);
        yy = y2;
    }
    free(buf);
    return err;
}

static esp_err_t spi_fill_rect_rgb565_bounds(int x, int y, int w, int h, uint16_t c, int bw, int bh)
{
    spi_clip_rect_to_bounds(&x, &y, &w, &h, bw, bh);
    if (w <= 0 || h <= 0) {
        return ESP_OK;
    }

    const int x0 = x;
    const int y0 = y;
    const int y1 = y0 + h;

    const int stripe = 16;
    size_t max_pix = (size_t)bw * (size_t)stripe;
    if (max_pix < (size_t)w) {
        max_pix = (size_t)w;
    }
    size_t bytes = max_pix * sizeof(uint16_t);
    uint16_t *buf = heap_caps_malloc(bytes, MALLOC_CAP_DMA);
    ESP_RETURN_ON_FALSE(buf, ESP_ERR_NO_MEM, TAG, "bounds fill buf");

    esp_err_t err = ESP_OK;
    for (int yy = y0; yy < y1 && err == ESP_OK;) {
        int y2 = yy + stripe;
        if (y2 > y1) {
            y2 = y1;
        }
        int rows = y2 - yy;
        int rw = w;
        int rx = x0;
        int ry = yy;
        spi_clip_rect_to_bounds(&rx, &ry, &rw, &rows, bw, bh);
        if (rw <= 0 || rows <= 0) {
            yy = y2;
            continue;
        }
        size_t npix = (size_t)rw * (size_t)rows;
        for (size_t i = 0; i < npix; i++) {
            buf[i] = c;
        }
        err = spi_address_window_native_mn(bw, bh);
        if (err != ESP_OK) {
            free(buf);
            return err;
        }
        err = esp_lcd_panel_draw_bitmap(s_panel, rx, ry, rx + rw, ry + rows, buf);
        if (err != ESP_OK) {
            free(buf);
            return err;
        }
        err = spi_address_window_native_mn(bw, bh);
        if (err != ESP_OK) {
            free(buf);
            return err;
        }
        yy = y2;
    }
    free(buf);
    return err;
}

void panel_hw_nuclear_clear(test_session_t *s)
{
    if (!s || !panel_hw_panel_ready()) {
        return;
    }
    if (panel_hw_is_spi() && panel_hw_bits_per_pixel() == 16) {
        int pw = (int)s_phys_w;
        int ph = (int)s_phys_h;
        if (pw <= 0 || ph <= 0) {
            uint16_t mw = 0, mh = 0;
            spi_presets_chip_gram_max(s_spi_chip, &mw, &mh);
            pw = (int)mw;
            ph = (int)mh;
        }
        if (pw > 0 && ph > 0) {
            panel_hw_set_orientation(s);
            (void)panel_hw_set_gap(0, 0);
            (void)esp_lcd_panel_swap_xy(s_panel, false);
            panel_hw_set_mirror(false, false);
            (void)spi_fill_rect_rgb565_bounds(0, 0, pw, ph, 0x0000u, pw, ph);
        }
        panel_hw_apply_gap(s);
        panel_hw_set_orientation(s);
        panel_hw_set_inversion(s);
        (void)spi_address_window_full_panel();
        vTaskDelay(pdMS_TO_TICKS(2));
    } else if (panel_hw_is_i2c()) {
        (void)panel_hw_fill_mono(0x00);
        vTaskDelay(pdMS_TO_TICKS(2));
    }
}

/* Preset-max GRAM fill in native axes; restore session gap/orient/invert after. */
static esp_err_t spi_paint_controller_gram_max_rgb565(const test_session_t *s, uint16_t rgb565)
{
    if (!s || !s_panel || s_kind != PHW_SPI || s_bpp != 16) {
        return ESP_ERR_INVALID_STATE;
    }

    uint16_t mw_u = 0, mh_u = 0;
    spi_presets_chip_gram_max(s_spi_chip, &mw_u, &mh_u);
    int mw = (int)mw_u;
    int mh = (int)mh_u;
    if (mw < (int)s_w) {
        mw = (int)s_w;
    }
    if (mh < (int)s_h) {
        mh = (int)s_h;
    }

    (void)panel_hw_set_gap(0, 0);
    (void)esp_lcd_panel_swap_xy(s_panel, false);
    panel_hw_set_mirror(false, false);

    esp_err_t e = spi_fill_rect_rgb565_bounds(0, 0, mw, mh, rgb565, mw, mh);

    panel_hw_apply_gap(s);
    panel_hw_apply_orientation(s);
    panel_hw_apply_invert(s);
    return e;
}

/*
 * Full logical viewport fill under current session orientation/invert (SPI: s_w×s_h).
 * Re-asserts session orientation and inversion before painting; callers often re-apply gap/orient/invert after.
 */
esp_err_t panel_hw_native_clear_gram_preset_max_rgb565(const test_session_t *s, uint16_t rgb565)
{
    if (!s) {
        return ESP_ERR_INVALID_ARG;
    }
    panel_hw_apply_orientation(s);
    panel_hw_apply_invert(s);
    return panel_hw_fill_rgb565(rgb565);
}

void panel_hw_spi_wipe_controller_gram_rgb565(const test_session_t *s, uint16_t rgb565)
{
    esp_err_t e = spi_paint_controller_gram_max_rgb565(s, rgb565);
    if (e != ESP_OK && e != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "controller GRAM wipe: %s", esp_err_to_name(e));
    }
}

/* 1 px outline of a wxh rectangle with top-left at (x,y), inclusive outer bounds. */
static esp_err_t spi_rect_outline_1px_rgb565(int x, int y, int bw, int bh, uint16_t c)
{
    if (bw <= 0 || bh <= 0) {
        return ESP_OK;
    }
    if (bw == 1) {
        return spi_vline_rgb565(x, y, bh, c);
    }
    if (bh == 1) {
        return spi_hline_rgb565(x, y, bw, c);
    }
    esp_err_t e = spi_hline_rgb565(x, y, bw, c);
    if (e != ESP_OK) {
        return e;
    }
    e = spi_hline_rgb565(x, y + bh - 1, bw, c);
    if (e != ESP_OK) {
        return e;
    }
    if (bh > 2) {
        e = spi_vline_rgb565(x, y + 1, bh - 2, c);
        if (e != ESP_OK) {
            return e;
        }
        e = spi_vline_rgb565(x + bw - 1, y + 1, bh - 2, c);
    }
    return e;
}

esp_err_t panel_hw_link_spi_rect_outline_1px_rgb565(int x, int y, int bw, int bh, uint16_t c)
{
    if (!s_panel || s_kind != PHW_SPI || s_bpp != 16) {
        return ESP_ERR_INVALID_STATE;
    }
    return spi_rect_outline_1px_rgb565(x, y, bw, bh, c);
}

esp_err_t panel_hw_link_spi_draw_bitmap(int x0, int y0, int x1, int y1, const uint16_t *rgb565)
{
    if (!s_panel || s_kind != PHW_SPI || s_bpp != 16 || !rgb565) {
        return ESP_ERR_INVALID_STATE;
    }
    return spi16_panel_draw_bitmap_atomic(x0, y0, x1, y1, rgb565);
}

esp_err_t panel_hw_draw_bitmap_rgb565(int x0, int y0, int x1, int y1, const uint16_t *rgb565)
{
    return panel_hw_link_spi_draw_bitmap(x0, y0, x1, y1, rgb565);
}

esp_err_t panel_hw_link_i2c_batch_verification_overlay_rgb565(int W, int H)
{
    if (!s_panel || s_kind != PHW_I2C || !s_i2c_shadow) {
        return ESP_ERR_INVALID_STATE;
    }
    int wclip = W;
    int hclip = H;
    if (wclip > (int)s_w) {
        wclip = (int)s_w;
    }
    if (hclip > (int)s_h) {
        hclip = (int)s_h;
    }
    const size_t n = (size_t)s_w * (size_t)s_h;
    memset(s_i2c_shadow, 0, n * sizeof(uint16_t));
    for (int y = 0; y < hclip; y++) {
        for (int x = 0; x < wclip; x++) {
            bool edge = (x == 0 || y == 0 || x == wclip - 1 || y == hclip - 1);
            uint16_t v = edge ? 0xFFFFu : ((((x + y) & 1) != 0) ? 0xFFFFu : 0x0000u);
            s_i2c_shadow[(size_t)y * s_w + (size_t)x] = v;
        }
    }
    return i2c_flush_shadow_to_panel();
}

esp_err_t panel_hw_link_i2c_top_marker_merge_band(int band_h, const uint16_t *buf, int buf_stride_px)
{
    if (!s_panel || s_kind != PHW_I2C || !s_i2c_shadow || !buf) {
        return ESP_ERR_INVALID_STATE;
    }
    int bh = band_h;
    if (bh > (int)s_h) {
        bh = (int)s_h;
    }
    for (int row = 0; row < bh; row++) {
        memcpy(&s_i2c_shadow[(size_t)row * s_w], &buf[(size_t)row * (size_t)buf_stride_px],
               (size_t)s_w * sizeof(uint16_t));
    }
    return i2c_flush_shadow_to_panel();
}

esp_err_t panel_hw_link_i2c_probe_marker_corner_from(const uint8_t *mono_1bpp_rowmajor, unsigned mono_w, unsigned mono_h)
{
    if (!s_panel || s_kind != PHW_I2C || !s_i2c_shadow || !mono_1bpp_rowmajor) {
        return ESP_ERR_INVALID_STATE;
    }
    if (mono_w < 8u || (mono_w % 8u) != 0u) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_w < mono_w || s_h < mono_h) {
        return ESP_OK;
    }
    const int x0 = (int)s_w - (int)mono_w;
    const int y0 = (int)s_h - (int)mono_h;
    const int row_bytes = (int)mono_w / 8;
    for (unsigned yy = 0; yy < mono_h; yy++) {
        for (unsigned xx = 0; xx < mono_w; xx++) {
            size_t bi = (size_t)yy * (size_t)row_bytes + (size_t)xx / 8u;
            unsigned bit = 7u - (unsigned)(xx & 7);
            bool on = (mono_1bpp_rowmajor[bi] >> bit) & 1u;
            int sx = x0 + (int)xx;
            int sy = y0 + (int)yy;
            s_i2c_shadow[(size_t)sy * s_w + (size_t)sx] = on ? 0xFFFFu : 0x0000u;
        }
    }
    return i2c_flush_shadow_to_panel();
}

esp_err_t panel_hw_link_i2c_checkerboard(int cell_px, uint16_t rgb565_a, uint16_t rgb565_b)
{
    if (!s_panel || s_kind != PHW_I2C || !s_i2c_shadow) {
        return ESP_ERR_INVALID_STATE;
    }
    int cell = cell_px;
    if (cell < 1) {
        cell = 1;
    }
    for (int y = 0; y < (int)s_h; y++) {
        for (int x = 0; x < (int)s_w; x++) {
            bool t = (((x / cell) ^ (y / cell)) & 1) != 0;
            s_i2c_shadow[(size_t)y * s_w + (size_t)x] = t ? rgb565_a : rgb565_b;
        }
    }
    return i2c_flush_shadow_to_panel();
}

static void rgb565_unpack_u16(uint16_t p, int *r, int *g, int *b)
{
    *r = (p >> 11) & 31;
    *g = (p >> 5) & 63;
    *b = (int)(p & 31);
}

static uint16_t rgb565_pack_u16(int r, int g, int b)
{
    if (r < 0) {
        r = 0;
    }
    if (r > 31) {
        r = 31;
    }
    if (g < 0) {
        g = 0;
    }
    if (g > 63) {
        g = 63;
    }
    if (b < 0) {
        b = 0;
    }
    if (b > 31) {
        b = 31;
    }
    return (uint16_t)(((uint16_t)r << 11) | ((uint16_t)g << 5) | (uint16_t)b);
}

esp_err_t panel_hw_link_i2c_gradient_vertical_rgb565(uint16_t top_rgb565, uint16_t bot_rgb565)
{
    if (!s_panel || s_kind != PHW_I2C || !s_i2c_shadow) {
        return ESP_ERR_INVALID_STATE;
    }
    int rt, gt, bt, rb, gb, bb;
    rgb565_unpack_u16(top_rgb565, &rt, &gt, &bt);
    rgb565_unpack_u16(bot_rgb565, &rb, &gb, &bb);
    const int denom = (s_h <= 1) ? 1 : ((int)s_h - 1);
    for (int y = 0; y < (int)s_h; y++) {
        uint16_t c = rgb565_pack_u16(rt + (rb - rt) * y / denom, gt + (gb - gt) * y / denom, bt + (bb - bt) * y / denom);
        for (int x = 0; x < (int)s_w; x++) {
            s_i2c_shadow[(size_t)y * s_w + (size_t)x] = c;
        }
    }
    return i2c_flush_shadow_to_panel();
}

esp_err_t panel_hw_fill_mono(uint8_t fill_byte)
{
    ESP_RETURN_ON_FALSE(s_panel && s_kind == PHW_I2C, ESP_ERR_INVALID_STATE, TAG, "i2c");
    ESP_RETURN_ON_FALSE(s_i2c_shadow, ESP_ERR_INVALID_STATE, TAG, "i2c shadow");
    const uint16_t px = (fill_byte != 0) ? 0xFFFFu : 0x0000u;
    const size_t n = (size_t)s_w * (size_t)s_h;
    for (size_t i = 0; i < n; i++) {
        s_i2c_shadow[i] = px;
    }
    return i2c_flush_shadow_to_panel();
}

void panel_hw_link_panel_dims(uint16_t *out_w, uint16_t *out_h)
{
    if (out_w) {
        *out_w = (s_panel != NULL) ? s_w : 0;
    }
    if (out_h) {
        *out_h = (s_panel != NULL) ? s_h : 0;
    }
}

esp_lcd_panel_io_handle_t panel_hw_link_get_io(void)
{
    return s_io;
}

esp_lcd_panel_handle_t panel_hw_link_get_panel(void)
{
    return s_panel;
}

session_spi_chip_t panel_hw_link_get_spi_chip(void)
{
    return s_spi_chip;
}

bool panel_hw_link_spi16_active(void)
{
    return s_panel != NULL && s_kind == PHW_SPI && s_bpp == 16;
}

/*
 * RGB565 fill in the current driver window (gap / swap_xy / mirror / invert unchanged).
 * Does not reset MADCTL or gap — unlike older “raw GRAM” paths that forced identity.
 */
esp_err_t panel_hw_draw_rect_raw(const test_session_t *s, int x0, int y0, int x1_inclusive, int y1_inclusive,
                                 uint16_t rgb565)
{
    ESP_RETURN_ON_FALSE(s, ESP_ERR_INVALID_ARG, TAG, "session");
    ESP_RETURN_ON_FALSE(panel_hw_link_spi16_active(), ESP_ERR_INVALID_STATE, TAG, "spi16");
    if (x1_inclusive < x0 || y1_inclusive < y0) {
        return ESP_ERR_INVALID_ARG;
    }
    const int w = x1_inclusive - x0 + 1;
    const int h = y1_inclusive - y0 + 1;
    return spi_fill_rect_rgb565(x0, y0, w, h, rgb565);
}

esp_err_t panel_hw_link_spi_hline_rgb565(int x, int y, int len, uint16_t c)
{
    if (!s_panel || s_kind != PHW_SPI || s_bpp != 16) {
        return ESP_ERR_INVALID_STATE;
    }
    return spi_hline_rgb565(x, y, len, c);
}

esp_err_t panel_hw_link_spi_vline_rgb565(int x, int y, int len, uint16_t c)
{
    if (!s_panel || s_kind != PHW_SPI || s_bpp != 16) {
        return ESP_ERR_INVALID_STATE;
    }
    return spi_vline_rgb565(x, y, len, c);
}

esp_err_t panel_hw_link_spi_fill_rect_rgb565(int x, int y, int w, int h, uint16_t c)
{
    if (!s_panel || s_kind != PHW_SPI || s_bpp != 16) {
        return ESP_ERR_INVALID_STATE;
    }
    return spi_fill_rect_rgb565(x, y, w, h, c);
}

esp_err_t panel_hw_link_spi_fill_rect_bounds(int x0, int y0, int w, int h, uint16_t rgb565, int bw, int bh)
{
    if (!s_panel || s_kind != PHW_SPI || s_bpp != 16) {
        return ESP_ERR_INVALID_STATE;
    }
    return spi_fill_rect_rgb565_bounds(x0, y0, w, h, rgb565, bw, bh);
}
