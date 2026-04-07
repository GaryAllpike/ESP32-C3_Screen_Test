/* Version: 0.9 (Pre-Release) | JBG Discovery Framework */
#include "session.h"
#include "panel_hw.h"
#include "spi_presets.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

void session_sync_mirror_from_silicon(test_session_t *s)
{
    if (!s) {
        return;
    }
    switch (s->silicon_mirror) {
    case PANEL_MIRROR_XY:
        s->mirror_x = true;
        s->mirror_y = true;
        break;
    case PANEL_MIRROR_X:
        s->mirror_x = true;
        s->mirror_y = false;
        break;
    case PANEL_MIRROR_Y:
        s->mirror_x = false;
        s->mirror_y = true;
        break;
    case PANEL_MIRROR_NONE:
    default:
        s->mirror_x = false;
        s->mirror_y = false;
        break;
    }
}

void session_init(test_session_t *s)
{
    memset(s, 0, sizeof(*s));
    s->bus = SESSION_BUS_UNKNOWN;
    s->transport_override = SESSION_TRANSPORT_AUTO;
    s->ssd1306_height = 64;
    s->spi_logical_rgb565[0] = 0xF800u;
    s->spi_logical_rgb565[1] = 0x07E0u;
    s->spi_logical_rgb565[2] = 0x001Fu;
}

void session_reset_display_fields(test_session_t *s)
{
    s->panel_ready = false;
    s->spi_chip = SESSION_SPI_CHIP_NONE;
    s->i2c_driver = SESSION_I2C_DRV_NONE;
    s->profile_tag[0] = '\0';
    s->hor_res = 0;
    s->ver_res = 0;
    s->phys_w = 0;
    s->phys_h = 0;
    s->silicon_mirror = PANEL_MIRROR_NONE;
    s->silicon_extent_red_hits_right = 0;
    s->gap_col = 0;
    s->gap_row = 0;
    s->rot_quarter = 0;
    s->mirror_x = false;
    s->mirror_y = false;
    s->inv_en = false;
    s->madctl = 0;
    s->spi_rgb_ele_order_rgb = false;
    s->spi_logical_rgb565[0] = 0xF800u;
    s->spi_logical_rgb565[1] = 0x07E0u;
    s->spi_logical_rgb565[2] = 0x001Fu;
    s->ssd1306_height = 64;
    s->backlight_pct = 0;
    s->spi_pclk_hz = 0;
    s->peak_spi_hz = 0;
    s->batch_dot_opens_provision = false;
    s->start_g10_after_provision = false;
    s->guided_override_next_stage = 0;
}

static const char *spi_chip_label(session_spi_chip_t c)
{
    switch (c) {
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
        return NULL;
    }
}

static const char *i2c_drv_label(session_i2c_driver_t d)
{
    switch (d) {
    case SESSION_I2C_DRV_SSD1306:
        return "SSD1306";
    case SESSION_I2C_DRV_SH1106:
        return "SH1106";
    default:
        return NULL;
    }
}

const char *session_model_label(const test_session_t *s)
{
    if (!s) {
        return "--";
    }
    if (s->bus == SESSION_BUS_SPI) {
        const char *c = spi_chip_label(s->spi_chip);
        return c ? c : "SPI";
    }
    if (s->bus == SESSION_BUS_I2C) {
        const char *d = i2c_drv_label(s->i2c_driver);
        return d ? d : "I2C";
    }
    return "--";
}

static const char *rot_deg_label(uint8_t q)
{
    switch (q & 3u) {
    case 0:
        return "0°";
    case 1:
        return "90°";
    case 2:
        return "180°";
    default:
        return "270°";
    }
}

static void session_print_display_truth_body(const test_session_t *s, const char *where_label, bool include_orient)
{
    if (!s || !where_label) {
        return;
    }
    printf("\n--- STATE ---\n");
    printf("%-15s : %s\n", "Context", where_label);
    if (s->bus == SESSION_BUS_SPI) {
        printf("%-15s : %s\n", "Bus", "SPI");
    } else if (s->bus == SESSION_BUS_I2C) {
        printf("%-15s : %s\n", "Bus", "I2C");
    }

    if (!s->panel_ready || !panel_hw_panel_ready()) {
        printf("%-15s : %s\n", "Panel", "not ready");
        return;
    }

    if (panel_hw_is_spi()) {
        const char *chip = spi_chip_label(s->spi_chip);
        if (chip) {
            printf("%-15s : %s\n", "SPI chip", chip);
        }
        if (s->hor_res > 0 && s->ver_res > 0) {
            printf("%-15s : %ux%u\n", "Resolution", (unsigned)s->hor_res, (unsigned)s->ver_res);
        }
        if (panel_hw_bits_per_pixel() == 16) {
            printf("%-15s : R=0x%04X G=0x%04X B=0x%04X\n", "Logical RGB565", (unsigned)s->spi_logical_rgb565[0],
                   (unsigned)s->spi_logical_rgb565[1], (unsigned)s->spi_logical_rgb565[2]);
            printf("%-15s : %s\n", "RGB bus order", s->spi_rgb_ele_order_rgb ? "RGB" : "BGR");
        }
        if (s->spi_pclk_hz > 0) {
            printf("%-15s : %" PRIu32 " Hz\n", "PCLK", s->spi_pclk_hz);
        }
    } else if (panel_hw_is_i2c()) {
        const char *drv = i2c_drv_label(s->i2c_driver);
        if (drv) {
            printf("%-15s : %s\n", "I2C driver", drv);
        }
        if (s->i2c_addr_7bit >= 0x08 && s->i2c_addr_7bit <= 0x77) {
            printf("%-15s : 0x%02X\n", "I2C address", (unsigned)s->i2c_addr_7bit);
        }
        if (s->hor_res > 0 && s->ver_res > 0) {
            printf("%-15s : %ux%u\n", "Resolution", (unsigned)s->hor_res, (unsigned)s->ver_res);
        }
    }

    if (include_orient) {
        printf("%-15s : %s\n", "Rotation", rot_deg_label(s->rot_quarter));
        printf("%-15s : %d\n", "Mirror X", s->mirror_x ? 1 : 0);
        printf("%-15s : %d\n", "Mirror Y", s->mirror_y ? 1 : 0);
        printf("%-15s : %s\n", "Invert", s->inv_en ? "on" : "off");
    }
    if (s->profile_tag[0] != '\0') {
        printf("%-15s : %s\n", "Profile", s->profile_tag);
    }
}

void session_print_display_truth(const test_session_t *s, const char *where_label)
{
    session_print_display_truth_body(s, where_label, true);
}

void session_print_display_truth_no_orient(const test_session_t *s, const char *where_label)
{
    session_print_display_truth_body(s, where_label, false);
}

void session_remap_gaps_after_orient_key(test_session_t *s, int key)
{
    if (!s || s->bus != SESSION_BUS_SPI || !panel_hw_panel_ready() || !panel_hw_is_spi() ||
        panel_hw_bits_per_pixel() != 16) {
        return;
    }
    uint16_t mw = 0, mh = 0;
    spi_presets_chip_gram_max(s->spi_chip, &mw, &mh);
    uint16_t v = s->hor_res;
    uint16_t h = s->ver_res;
    if (v == 0 || h == 0) {
        return;
    }
    if (key == 'o') {
        int16_t t = s->gap_col;
        s->gap_col = s->gap_row;
        s->gap_row = t;
        return;
    }
    if (key == 'x' && mw > v) {
        s->gap_col = (int16_t)((int32_t)mw - (int32_t)v - (int32_t)s->gap_col);
    }
    if (key == 'y' && mh > h) {
        s->gap_row = (int16_t)((int32_t)mh - (int32_t)h - (int32_t)s->gap_row);
    }
}
