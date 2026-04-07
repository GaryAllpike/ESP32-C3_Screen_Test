/* Version: 0.9 (Pre-Release) | JBG Discovery Framework */
#include "panel_hw.h"
#include "panel_hw_link.h"
#include "spi_presets.h"
#include "ui_colors.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_commands.h"
#include "esp_lcd_panel_io.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "panel_assets.h"
#include "panel_probes.h"
#include "session.h"

static const char *TAG = "panel_probes";

/*
 * Searchlight marker: 40×40 white tile at origin with a 3px-stroke “F” (black).
 * RAW: panel_hw_draw_rect_raw — silicon GRAM (0,0); logical: session gap / orient / inv.
 */
#define PROBE_MARKER_BOX 40
#define PROBE_F_STROKE 3
#define SAFE_SQUARE_DIM 128

static esp_err_t probes_draw_marker_f_stroke3_raw(const test_session_t *s, uint16_t fg_rgb565, int x_off, int y_off)
{
    esp_err_t e;
    e = panel_hw_draw_rect_raw(s, x_off + 4, y_off + 4, x_off + 6, y_off + 35, fg_rgb565);
    if (e != ESP_OK) {
        return e;
    }
    e = panel_hw_draw_rect_raw(s, x_off + 4, y_off + 4, x_off + 35, y_off + 6, fg_rgb565);
    if (e != ESP_OK) {
        return e;
    }
    return panel_hw_draw_rect_raw(s, x_off + 4, y_off + 17, x_off + 22, y_off + 19, fg_rgb565);
}

static esp_err_t probes_draw_marker_f_stroke3_logical_offset(int x_off, int y_off, uint16_t fg_rgb565)
{
    esp_err_t e;
    e = panel_hw_link_spi_fill_rect_rgb565(x_off + 4, y_off + 4, PROBE_F_STROKE, 32, fg_rgb565);
    if (e != ESP_OK) {
        return e;
    }
    e = panel_hw_link_spi_fill_rect_rgb565(x_off + 4, y_off + 4, 32, PROBE_F_STROKE, fg_rgb565);
    if (e != ESP_OK) {
        return e;
    }
    return panel_hw_link_spi_fill_rect_rgb565(x_off + 4, y_off + 17, 19, PROBE_F_STROKE, fg_rgb565);
}

static esp_err_t probes_draw_marker_f_stroke3_logical(uint16_t fg_rgb565)
{
    return probes_draw_marker_f_stroke3_logical_offset(0, 0, fg_rgb565);
}

/*
 * Caliper / compass discovery: CASET/RASET to full preset GRAM; MADCTL 0x00 True North.
 */
static esp_err_t probes_push_silicon_identity_hardware(void)
{
    esp_lcd_panel_io_handle_t io = panel_hw_link_get_io();
    esp_lcd_panel_handle_t panel = panel_hw_link_get_panel();
    if (!io || !panel) {
        return ESP_ERR_INVALID_STATE;
    }
    (void)panel_hw_set_gap(0, 0);
    (void)esp_lcd_panel_swap_xy(panel, false);
    panel_hw_set_mirror(false, false);

    const uint8_t madctl_identity = 0x00;
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, LCD_CMD_MADCTL, &madctl_identity, 1), TAG, "MADCTL");

    uint16_t mw_u = 0, mh_u = 0;
    spi_presets_chip_gram_max(panel_hw_link_get_spi_chip(), &mw_u, &mh_u);
    if (mw_u == 0 || mh_u == 0) {
        return ESP_ERR_INVALID_SIZE;
    }
    uint16_t xe = (uint16_t)(mw_u - 1u);
    uint16_t ye = (uint16_t)(mh_u - 1u);

    const uint8_t caset[4] = { 0x00, 0x00, (uint8_t)(xe >> 8), (uint8_t)(xe & 0xFF) };
    const uint8_t raset[4] = { 0x00, 0x00, (uint8_t)(ye >> 8), (uint8_t)(ye & 0xFF) };
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, LCD_CMD_CASET, caset, sizeof(caset)), TAG, "CASET");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, LCD_CMD_RASET, raset, sizeof(raset)), TAG, "RASET");
    return ESP_OK;
}

static esp_err_t spi_paint_discovery_gram_rgb565(const test_session_t *s, uint16_t rgb565)
{
    if (!s || !panel_hw_link_spi16_active()) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t e = panel_hw_native_clear_gram_preset_max_rgb565(s, rgb565);
    if (e != ESP_OK) {
        panel_hw_apply_gap(s);
        panel_hw_apply_orientation(s);
        panel_hw_apply_invert(s);
        return e;
    }

    panel_hw_apply_gap(s);
    panel_hw_apply_orientation(s);
    panel_hw_apply_invert(s);
    return ESP_OK;
}

esp_err_t panel_hw_spi_paint_discovery_gram_rgb565(const test_session_t *s, uint16_t rgb565)
{
    return spi_paint_discovery_gram_rgb565(s, rgb565);
}

esp_err_t panel_hw_draw_rect_raw(const test_session_t *s, int x0, int y0, int x1_inclusive, int y1_inclusive,
                                 uint16_t rgb565)
{
    ESP_RETURN_ON_FALSE(s && panel_hw_link_spi16_active(), ESP_ERR_INVALID_STATE, TAG, "spi16");
    if (x1_inclusive < x0 || y1_inclusive < y0) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_lcd_panel_handle_t panel = panel_hw_link_get_panel();
    (void)panel_hw_set_gap(0, 0);
    (void)esp_lcd_panel_swap_xy(panel, false);
    panel_hw_set_mirror(false, false);

    uint16_t mw_u = 0, mh_u = 0;
    spi_presets_chip_gram_max(panel_hw_link_get_spi_chip(), &mw_u, &mh_u);
    int mw = (int)mw_u;
    int mh = (int)mh_u;
    if (mw <= 0 || mh <= 0) {
        return ESP_ERR_INVALID_SIZE;
    }

    int w = x1_inclusive - x0 + 1;
    int h = y1_inclusive - y0 + 1;
    return panel_hw_link_spi_fill_rect_bounds(x0, y0, w, h, rgb565, mw, mh);
}

esp_err_t panel_hw_draw_silicon_probe(const test_session_t *s)
{
    ESP_RETURN_ON_FALSE(s && panel_hw_link_spi16_active(), ESP_ERR_INVALID_STATE, TAG, "spi16");

    panel_hw_spi_clear_hardware_gap();

    esp_err_t e = probes_push_silicon_identity_hardware();
    if (e != ESP_OK) {
        ESP_LOGW(TAG, "silicon identity: %s", esp_err_to_name(e));
    }

    ESP_RETURN_ON_ERROR(panel_hw_draw_rect_raw(s, 0, 0, SAFE_SQUARE_DIM - 1, SAFE_SQUARE_DIM - 1, 0x0000u), TAG,
                        "safe square clear");

    const int m_offset = (SAFE_SQUARE_DIM - PROBE_MARKER_BOX) / 2;
    ESP_RETURN_ON_ERROR(panel_hw_draw_rect_raw(s, m_offset, m_offset, m_offset + PROBE_MARKER_BOX - 1,
                                               m_offset + PROBE_MARKER_BOX - 1, 0xFFFFu),
                        TAG, "marker box");

    return probes_draw_marker_f_stroke3_raw(s, 0x0000u, m_offset, m_offset);
}

/*
 * Searchlight F marker: 40×40 white at logical origin, 3px black F (visible through inv_en).
 */
esp_err_t panel_hw_draw_marker_probe_rgb565(const test_session_t *s)
{
    ESP_RETURN_ON_FALSE(s && panel_hw_link_spi16_active(), ESP_ERR_INVALID_STATE, TAG, "spi16");

    panel_hw_apply_gap(s);
    panel_hw_apply_orientation(s);
    panel_hw_apply_invert(s);

    int W = (int)s->hor_res;
    int H = (int)s->ver_res;
    if (W <= 0 || H <= 0) {
        return ESP_ERR_INVALID_SIZE;
    }

    const uint16_t bg = ui_color_bg(s);
    const uint16_t white = 0xFFFFu;
    const uint16_t f_dark = 0x0000u;

    esp_err_t e = panel_hw_link_spi_fill_rect_rgb565(0, 0, W, H, bg);
    if (e != ESP_OK) {
        return e;
    }
    e = panel_hw_link_spi_fill_rect_rgb565(0, 0, PROBE_MARKER_BOX, PROBE_MARKER_BOX, white);
    if (e != ESP_OK) {
        return e;
    }
    return probes_draw_marker_f_stroke3_logical(f_dark);
}

static esp_err_t stage3_extent_draw_axes_rgb565(int W, int H, uint16_t bg, bool lines_visible)
{
    const uint16_t red = 0xF800u;
    const uint16_t blue = 0x001Fu;
    const int ins = 10;
    int x0 = ins;
    int y0 = ins;
    int line_w = W - 2 * ins;
    int line_h = H - 2 * ins;
    if (line_w < 3 || line_h < 3) {
        x0 = 0;
        y0 = 0;
        line_w = W;
        line_h = H;
    }

    esp_err_t e = panel_hw_link_spi_fill_rect_rgb565(0, 0, W, H, bg);
    if (e != ESP_OK || !lines_visible) {
        return e;
    }
    for (int dy = 0; dy < 3 && e == ESP_OK; dy++) {
        e = panel_hw_link_spi_hline_rgb565(x0, y0 + dy, line_w, red);
    }
    for (int dx = 0; dx < 3 && e == ESP_OK; dx++) {
        e = panel_hw_link_spi_vline_rgb565(x0 + dx, y0, line_h, blue);
    }
    return e;
}

esp_err_t panel_hw_draw_stage3_extent_probe_rgb565(const test_session_t *s)
{
    ESP_RETURN_ON_FALSE(s && panel_hw_link_spi16_active(), ESP_ERR_INVALID_STATE, TAG, "spi16");

    panel_hw_apply_gap(s);
    panel_hw_apply_orientation(s);

    int W = (int)s->hor_res;
    int H = (int)s->ver_res;
    if (W <= 0 || H <= 0) {
        return ESP_ERR_INVALID_SIZE;
    }

    const uint16_t green = ui_color_primary_g(s);

    /* 500 ms toggle: axes on, then off, then steady on (red X / blue Y = 3 px thick). */
    esp_err_t e = stage3_extent_draw_axes_rgb565(W, H, green, true);
    if (e != ESP_OK) {
        return e;
    }
    vTaskDelay(pdMS_TO_TICKS(500));
    e = stage3_extent_draw_axes_rgb565(W, H, green, false);
    if (e != ESP_OK) {
        return e;
    }
    vTaskDelay(pdMS_TO_TICKS(500));
    return stage3_extent_draw_axes_rgb565(W, H, green, true);
}

/* --- Patterns, G5/G6, Phase 0b, orientation (moved from panel_hw.c) --- */

/* Perceived luma (0..255) → ui_color_bg vs ui_color_text_normal (no hardcoded white). */
static uint16_t probes_rgb565_high_contrast_label_fg(const test_session_t *s, uint16_t bg)
{
    unsigned r5 = (unsigned)(bg >> 11) & 0x1Fu;
    unsigned g6 = (unsigned)(bg >> 5) & 0x3Fu;
    unsigned b5 = (unsigned)bg & 0x1Fu;
    unsigned r = (r5 * 255u) / 31u;
    unsigned g = (g6 * 255u) / 63u;
    unsigned b = (b5 * 255u) / 31u;
    unsigned y = (77u * r + 150u * g + 29u * b) >> 8;
    return (y > 115u) ? ui_color_bg(s) : ui_color_text_normal(s);
}

/*
 * Adafruit_GFX glcdfont glyphs 0x20..0x5A (column-major, 5 bytes per character).
 * Source: https://github.com/adafruit/Adafruit-GFX-Library/blob/master/glcdfont.c (BSD)
 */
static const uint8_t k_font5x7_space_z[] = {
#include "font5x7_space_to_z.inc"
};

static void probes_rgb565_fill_rect_buf(uint16_t *buf, int bw, int bh, int x, int y, int rw, int rh, uint16_t fg)
{
    for (int yy = y; yy < y + rh; yy++) {
        if (yy < 0 || yy >= bh) {
            continue;
        }
        for (int xx = x; xx < x + rw; xx++) {
            if (xx >= 0 && xx < bw) {
                buf[yy * bw + xx] = fg;
            }
        }
    }
}

static void probes_font5x7_blit_scaled(uint16_t *buf, int bw, int bh, int ox, int oy, const char *text, int scale,
                                       uint16_t fg)
{
    if (!text || scale < 1) {
        return;
    }
    size_t n = strlen(text);
    if (n == 0) {
        return;
    }
    int gap_px = scale;
    int cell = 5 * scale + gap_px;
    for (size_t ci = 0; ci < n; ci++) {
        unsigned char ch = (unsigned char)text[ci];
        if (ch >= 'a' && ch <= 'z') {
            ch = (unsigned char)(ch - 'a' + 'A');
        }
        if (ch < ' ' || ch > 'Z') {
            ch = ' ';
        }
        size_t gidx = (size_t)(ch - ' ') * 5u;
        int char_ox = ox + (int)ci * cell;
        for (int col = 0; col < 5; col++) {
            uint8_t column_bits = k_font5x7_space_z[gidx + (size_t)col];
            for (int row = 0; row < 7; row++) {
                if ((column_bits >> row) & 1u) {
                    probes_rgb565_fill_rect_buf(buf, bw, bh, char_ox + col * scale, oy + row * scale, scale, scale,
                                                fg);
                }
            }
        }
    }
}

esp_err_t panel_hw_draw_batch_verification_overlay_rgb565(const test_session_t *s)
{
    ESP_RETURN_ON_FALSE(s && panel_hw_panel_ready(), ESP_ERR_INVALID_ARG, TAG, "args");

    panel_hw_apply_gap(s);
    panel_hw_apply_orientation(s);

    int W = (int)s->hor_res;
    int H = (int)s->ver_res;
    if (W <= 0 || H <= 0) {
        return ESP_ERR_INVALID_ARG;
    }

    uint16_t sw = 0, sh = 0;
    panel_hw_link_panel_dims(&sw, &sh);
    if (sw == 0 || sh == 0) {
        return ESP_ERR_INVALID_STATE;
    }
    if (W > (int)sw) {
        W = (int)sw;
    }
    if (H > (int)sh) {
        H = (int)sh;
    }

    if (panel_hw_is_i2c()) {
        return panel_hw_link_i2c_batch_verification_overlay_rgb565(W, H);
    }

    ESP_RETURN_ON_FALSE(panel_hw_is_spi() && panel_hw_bits_per_pixel() == 16, ESP_ERR_INVALID_STATE, TAG, "spi16");

    const uint16_t white = 0xFFFFu;
    const uint16_t gray = 0x8410u;
    if (W >= 3 && H >= 3) {
        ESP_RETURN_ON_ERROR(panel_hw_link_spi_fill_rect_rgb565(1, 1, W - 2, H - 2, gray), TAG, "g10 gray");
    } else {
        ESP_RETURN_ON_ERROR(panel_hw_link_spi_fill_rect_rgb565(0, 0, W, H, gray), TAG, "g10 gray small");
    }
    return panel_hw_link_spi_rect_outline_1px_rgb565(0, 0, W, H, white);
}

esp_err_t panel_hw_draw_g5_alignment_pattern(const test_session_t *s)
{
    ESP_RETURN_ON_FALSE(panel_hw_is_spi(), ESP_ERR_INVALID_STATE, TAG, "spi");
    ESP_RETURN_ON_FALSE(s, ESP_ERR_INVALID_ARG, TAG, "session");

    uint16_t pw_u = 0, ph_u = 0;
    panel_hw_spi_fb_size(&pw_u, &ph_u);
    const int W = (int)pw_u;
    const int H = (int)ph_u;
    if (W <= 0 || H <= 0) {
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(panel_hw_link_spi_fill_rect_rgb565(0, 0, W, H, ui_color_g5_interior(s)), TAG, "g5 base");

    if (W < 7 || H < 7) {
        return panel_hw_link_spi_rect_outline_1px_rgb565(0, 0, W, H, ui_color_g5_outer(s));
    }

    ESP_RETURN_ON_ERROR(panel_hw_link_spi_rect_outline_1px_rgb565(0, 0, W, H, ui_color_g5_outer(s)), TAG, "g5 c1");
    ESP_RETURN_ON_ERROR(panel_hw_link_spi_rect_outline_1px_rgb565(1, 1, W - 2, H - 2, ui_color_g5_mid(s)), TAG,
                        "g5 c2");
    ESP_RETURN_ON_ERROR(panel_hw_link_spi_rect_outline_1px_rgb565(2, 2, W - 4, H - 4, ui_color_g5_inner(s)), TAG,
                        "g5 c3");
    return ESP_OK;
}

esp_err_t panel_hw_draw_g5_origin_ballpark_rgb565(const test_session_t *s)
{
    ESP_RETURN_ON_FALSE(panel_hw_link_spi16_active(), ESP_ERR_INVALID_STATE, TAG, "spi16");
    ESP_RETURN_ON_FALSE(s, ESP_ERR_INVALID_ARG, TAG, "session");

    const int16_t gc = s->gap_col, gr = s->gap_row;
    ESP_RETURN_ON_ERROR(panel_hw_set_gap(0, 0), TAG, "gap0");

    uint16_t pw_u = 0, ph_u = 0;
    panel_hw_spi_fb_size(&pw_u, &ph_u);
    const int W = (int)pw_u;
    const int H = (int)ph_u;
    const int th = (H < 20) ? H : 20;
    const int tw = (W < 20) ? W : 20;
    const uint16_t cr = ui_color_alert(s);
    const uint16_t cg = ui_color_primary_g(s);
    const uint16_t cb = ui_color_primary_b(s);
    const uint16_t cm = ui_color_magenta(s);

    for (int seg = 0; seg < 4; seg++) {
        int x0 = seg * 20;
        if (x0 >= W || th <= 0) {
            break;
        }
        int x1 = (seg == 3) ? W : ((seg + 1) * 20 < W ? (seg + 1) * 20 : W);
        if (x1 <= x0) {
            break;
        }
        uint16_t c = (seg == 0) ? cr : (seg == 1) ? cg : (seg == 2) ? cb : cm;
        esp_err_t e = panel_hw_link_spi_fill_rect_rgb565(x0, 0, x1 - x0, th, c);
        if (e != ESP_OK) {
            (void)panel_hw_set_gap(gc, gr);
            return e;
        }
    }

    for (int seg = 0; seg < 4; seg++) {
        int y0 = seg * 20;
        if (y0 >= H || tw <= 0) {
            break;
        }
        int y1 = (seg == 3) ? H : ((seg + 1) * 20 < H ? (seg + 1) * 20 : H);
        if (y1 <= y0) {
            break;
        }
        uint16_t c = (seg == 0) ? cr : (seg == 1) ? cg : (seg == 2) ? cb : cm;
        esp_err_t e = panel_hw_link_spi_fill_rect_rgb565(0, y0, tw, y1 - y0, c);
        if (e != ESP_OK) {
            (void)panel_hw_set_gap(gc, gr);
            return e;
        }
    }

    return panel_hw_set_gap(gc, gr);
}

/*
 * G5 caliper / compass: logical (0,0) white tile + F after preset-max black wipe; session gap/orient/inv applied.
 */
esp_err_t panel_hw_draw_compass_verify(const test_session_t *s)
{
    ESP_RETURN_ON_FALSE(s && panel_hw_link_spi16_active(), ESP_ERR_INVALID_STATE, TAG, "spi16");

    panel_hw_apply_gap(s);
    panel_hw_apply_orientation(s);
    panel_hw_apply_invert(s);

    esp_err_t e = panel_hw_native_clear_gram_preset_max_rgb565(s, 0x0000u);

    panel_hw_apply_gap(s);
    panel_hw_apply_orientation(s);
    panel_hw_apply_invert(s);

    if (e != ESP_OK) {
        return e;
    }

    int W = (int)s->hor_res;
    int H = (int)s->ver_res;
    if (W <= 0 || H <= 0) {
        return ESP_ERR_INVALID_SIZE;
    }

    ESP_RETURN_ON_ERROR(panel_hw_link_spi_fill_rect_rgb565(0, 0, W, H, 0x0000u), TAG, "logical clear");

    const int box_size = PROBE_MARKER_BOX;
    ESP_RETURN_ON_ERROR(panel_hw_link_spi_fill_rect_rgb565(0, 0, box_size, box_size, 0xFFFFu), TAG, "origin box");

    return probes_draw_marker_f_stroke3_logical_offset(0, 0, 0x0000u);
}

esp_err_t panel_hw_draw_g5_origin_probe_rgb565(const test_session_t *s)
{
    return panel_hw_draw_compass_verify(s);
}

esp_err_t panel_hw_draw_g6_extents_ballpark_rgb565(const test_session_t *s)
{
    ESP_RETURN_ON_FALSE(panel_hw_link_spi16_active(), ESP_ERR_INVALID_STATE, TAG, "spi16");
    ESP_RETURN_ON_FALSE(s, ESP_ERR_INVALID_ARG, TAG, "session");

    ESP_RETURN_ON_ERROR(panel_hw_fill_rgb565(ui_color_bg(s)), TAG, "clr");
    const uint16_t c_mag = ui_color_magenta(s);
    const uint16_t c_cya = ui_color_cyan(s);
    const uint16_t c_grn = ui_color_primary_g(s);
    const uint16_t c_red = ui_color_alert(s);

    const struct {
        int w, h;
        uint16_t c;
    } layers[] = {
        { 240, 320, c_mag },
        { 135, 240, c_cya },
        { 128, 160, c_grn },
        { 128, 128, c_red },
    };

    for (size_t i = 0; i < sizeof(layers) / sizeof(layers[0]); i++) {
        ESP_RETURN_ON_ERROR(panel_hw_link_spi_fill_rect_rgb565(0, 0, layers[i].w, layers[i].h, layers[i].c), TAG,
                            "nest");
    }
    return ESP_OK;
}

esp_err_t panel_hw_draw_g6_extents_probe_rgb565(const test_session_t *s, uint16_t log_w, uint16_t log_h)
{
    ESP_RETURN_ON_FALSE(panel_hw_link_spi16_active(), ESP_ERR_INVALID_STATE, TAG, "spi16");
    ESP_RETURN_ON_FALSE(s, ESP_ERR_INVALID_ARG, TAG, "session");

    int w = (int)log_w;
    int h = (int)log_h;
    if (w < 2) {
        w = 2;
    }
    if (h < 2) {
        h = 2;
    }

    ESP_RETURN_ON_ERROR(panel_hw_fill_rgb565(ui_color_bg(s)), TAG, "clr");
    const uint16_t hi = ui_color_highlight(s);
    ESP_RETURN_ON_ERROR(panel_hw_link_spi_fill_rect_rgb565(w - 2, 0, 2, h, hi), TAG, "rv");
    ESP_RETURN_ON_ERROR(panel_hw_link_spi_fill_rect_rgb565(0, h - 2, w, 2, hi), TAG, "rh");
    return ESP_OK;
}

esp_err_t panel_hw_draw_top_marker(const test_session_t *s)
{
    ESP_RETURN_ON_FALSE(panel_hw_panel_ready() && (panel_hw_is_spi() || panel_hw_is_i2c()), ESP_ERR_INVALID_STATE,
                        TAG, "draw");
    ESP_RETURN_ON_FALSE(s, ESP_ERR_INVALID_ARG, TAG, "session");
    const uint16_t band_fg = ui_color_highlight(s);

    if (panel_hw_is_spi() && panel_hw_bits_per_pixel() == 16) {
        ESP_RETURN_ON_ERROR(panel_hw_spi_paint_discovery_gram_rgb565(s, ui_color_bg(s)), TAG, "fill disc");
    } else {
        ESP_RETURN_ON_ERROR(panel_hw_fill_rgb565(ui_color_bg(s)), TAG, "fill");
    }

    uint16_t sw = 0, sh = 0;
    panel_hw_link_panel_dims(&sw, &sh);
    if (sw == 0 || sh == 0) {
        return ESP_ERR_INVALID_STATE;
    }
    const int band = 12;
    if (band > (int)sh) {
        return ESP_OK;
    }
    size_t pix = (size_t)sw * (unsigned)band;
    size_t bytes = pix * sizeof(uint16_t);
    uint16_t *buf = heap_caps_malloc(bytes, MALLOC_CAP_DMA);
    ESP_RETURN_ON_FALSE(buf, ESP_ERR_NO_MEM, TAG, "buf");
    for (size_t i = 0; i < pix; i++) {
        buf[i] = band_fg;
    }

    static const char k_top_label[] = "TOP";
    const size_t n = strlen(k_top_label);
    const int glyph_h = 7;
    int scale = 2;
    int total_w = 0;
    int total_h = 0;
    for (;;) {
        int gap_px = scale;
        int cell = 5 * scale + gap_px;
        total_w = (int)n * cell - gap_px;
        total_h = glyph_h * scale;
        if (total_w > 0 && total_w <= (int)sw && total_h > 0 && total_h <= band) {
            break;
        }
        scale--;
        if (scale < 1) {
            total_w = 0;
            break;
        }
    }
    if (total_w > 0) {
        int ox = ((int)sw - total_w) / 2;
        int oy = (band - total_h) / 2;
        uint16_t fg = probes_rgb565_high_contrast_label_fg(s, band_fg);
        probes_font5x7_blit_scaled(buf, (int)sw, band, ox, oy, k_top_label, scale, fg);
    }

    esp_err_t e;
    if (panel_hw_is_spi()) {
        e = panel_hw_draw_bitmap_rgb565(0, 0, (int)sw, band, buf);
    } else {
        e = panel_hw_link_i2c_top_marker_merge_band(band, buf, (int)sw);
    }
    free(buf);
    return e;
}

esp_err_t panel_hw_draw_orientation_up_probe(const test_session_t *s)
{
    /* Legacy API name; draws searchlight F marker. */
    return panel_hw_draw_marker_probe_rgb565(s);
}

void panel_hw_sync_orientation_up_probe(const test_session_t *s)
{
    if (!s || !panel_hw_panel_ready() || !panel_hw_is_spi() || panel_hw_bits_per_pixel() != 16) {
        return;
    }
    esp_err_t e = panel_hw_draw_orientation_up_probe(s);
    if (e != ESP_OK) {
        ESP_LOGW(TAG, "orientation probe sync: %s", esp_err_to_name(e));
    }
}

static esp_err_t probes_spi_draw_primary_label_top_left(const char *text, uint16_t bg, uint16_t fg)
{
    ESP_RETURN_ON_FALSE(panel_hw_link_spi16_active(), ESP_ERR_INVALID_STATE, TAG, "spi16");

    size_t n = strlen(text);
    ESP_RETURN_ON_FALSE(n > 0, ESP_ERR_INVALID_ARG, TAG, "text");

    uint16_t pw_u = 0, ph_u = 0;
    panel_hw_spi_fb_size(&pw_u, &ph_u);
    const int margin = 2;
    const int avail_w = (int)pw_u - margin;
    const int avail_h = (int)ph_u - margin;
    ESP_RETURN_ON_FALSE(avail_w > 0 && avail_h > 0, ESP_ERR_INVALID_SIZE, TAG, "panel too small");

    const int glyph_h = 7;
    int scale = 2;
    int total_w = 0;
    int total_h = 0;
    int gap_px = 0;
    int cell = 0;

    for (;;) {
        gap_px = scale;
        cell = 5 * scale + gap_px;
        total_w = (int)n * cell - gap_px;
        total_h = glyph_h * scale;
        if (total_w > 0 && total_w <= avail_w && total_h > 0 && total_h <= avail_h) {
            break;
        }
        scale--;
        ESP_RETURN_ON_FALSE(scale >= 1, ESP_ERR_INVALID_SIZE, TAG, "panel too small for label");
    }

    const int x0 = margin;
    const int y0 = margin;

    size_t npix = (size_t)total_w * (size_t)total_h;
    size_t bytes = npix * sizeof(uint16_t);
    uint16_t *buf = heap_caps_malloc(bytes, MALLOC_CAP_DMA);
    ESP_RETURN_ON_FALSE(buf, ESP_ERR_NO_MEM, TAG, "label buf");

    for (size_t i = 0; i < npix; i++) {
        buf[i] = bg;
    }

    probes_font5x7_blit_scaled(buf, total_w, total_h, 0, 0, text, scale, fg);

    esp_err_t err = panel_hw_draw_bitmap_rgb565(x0, y0, x0 + total_w, y0 + total_h, buf);
    free(buf);
    return err;
}

esp_err_t panel_hw_spi_run_phase0b_rgb_demo(const test_session_t *s)
{
    ESP_RETURN_ON_FALSE(panel_hw_link_spi16_active(), ESP_ERR_INVALID_STATE, TAG, "spi16");
    ESP_RETURN_ON_FALSE(s, ESP_ERR_INVALID_ARG, TAG, "session");

    static const char *const k_semantic[3] = { "RED", "GREEN", "BLUE" };

    for (size_t i = 0; i < 3; i++) {
        uint16_t bg = s->spi_logical_rgb565[i];
        uint16_t fg = probes_rgb565_high_contrast_label_fg(s, bg);
        ESP_RETURN_ON_ERROR(panel_hw_spi_paint_discovery_gram_rgb565(s, bg), TAG, "primary gram fill");
        ESP_RETURN_ON_ERROR(probes_spi_draw_primary_label_top_left(k_semantic[i], bg, fg), TAG, "label");
        vTaskDelay(pdMS_TO_TICKS(600));
    }
    return ESP_OK;
}

esp_err_t panel_hw_spi_run_phase0b_secondaries_demo(const test_session_t *s)
{
    ESP_RETURN_ON_FALSE(panel_hw_link_spi16_active(), ESP_ERR_INVALID_STATE, TAG, "spi16");
    ESP_RETURN_ON_FALSE(s, ESP_ERR_INVALID_ARG, TAG, "session");

    const uint16_t r = s->spi_logical_rgb565[0];
    const uint16_t g = s->spi_logical_rgb565[1];
    const uint16_t b = s->spi_logical_rgb565[2];

    assert((r & g) == 0);
    assert((g & b) == 0);
    assert((r & b) == 0);

    ESP_RETURN_ON_ERROR(panel_hw_spi_paint_discovery_gram_rgb565(s, ui_color_bg(s)), TAG, "sec pre-blackout");

    for (int si = 0; si < 4; si++) {
        uint16_t bg;
        const char *lab;
        switch (si) {
        case 0:
            bg = (uint16_t)(r | g);
            lab = "YELLOW";
            break;
        case 1:
            bg = (uint16_t)(g | b);
            lab = "CYAN";
            break;
        case 2:
            bg = (uint16_t)(r | b);
            lab = "MAGENTA";
            break;
        default:
            bg = (uint16_t)(r | g | b);
            lab = "WHITE";
            break;
        }
        uint16_t fg = (si == 1) ? ui_color_bg(s) : probes_rgb565_high_contrast_label_fg(s, bg);
        ESP_RETURN_ON_ERROR(panel_hw_spi_paint_discovery_gram_rgb565(s, bg), TAG, "secondary gram fill");
        ESP_RETURN_ON_ERROR(probes_spi_draw_primary_label_top_left(lab, bg, fg), TAG, "sec label");
        vTaskDelay(pdMS_TO_TICKS(600));
    }
    return ESP_OK;
}

void panel_probes_g4_dispatch_orientation_key(test_session_t *s, int k)
{
    if (!s) {
        return;
    }
    switch (k) {
    case 'r':
        s->rot_quarter = (uint8_t)((s->rot_quarter + 1) & 3);
        session_remap_gaps_after_orient_key(s, 'o');
        panel_hw_set_orientation(s);
        break;
    case 'a':
    case 'd':
        s->mirror_x = !s->mirror_x;
        session_remap_gaps_after_orient_key(s, 'x');
        panel_hw_set_orientation(s);
        break;
    case 'w':
    case 's':
        s->mirror_y = !s->mirror_y;
        session_remap_gaps_after_orient_key(s, 'y');
        panel_hw_set_orientation(s);
        break;
    case 'i':
        s->inv_en = !s->inv_en;
        panel_hw_set_inversion(s);
        break;
    default:
        break;
    }
}

/* --- Validation probes (session gap / orientation / inv_en; extent vs panel + phys caps) --- */

static void probe_apply_session_transform(const test_session_t *s)
{
    panel_hw_apply_gap(s);
    panel_hw_apply_orientation(s);
    panel_hw_apply_invert(s);
}

static void probes_rgb565_unpack(uint16_t p, int *r, int *g, int *b)
{
    *r = (p >> 11) & 31;
    *g = (p >> 5) & 63;
    *b = (int)(p & 31);
}

static uint16_t probes_rgb565_pack(int r, int g, int b)
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

static void probes_validation_extents(const test_session_t *s, int *bw, int *bh)
{
    uint16_t pw = 0, ph = 0;
    panel_hw_spi_fb_size(&pw, &ph);
    int w = (int)s->hor_res;
    int h = (int)s->ver_res;
    if (w < 1 || h < 1) {
        w = (int)pw;
        h = (int)ph;
    }
    if (s->phys_w > 0 && w > (int)s->phys_w) {
        w = (int)s->phys_w;
    }
    if (s->phys_h > 0 && h > (int)s->phys_h) {
        h = (int)s->phys_h;
    }
    if ((int)pw > 0 && w > (int)pw) {
        w = (int)pw;
    }
    if ((int)ph > 0 && h > (int)ph) {
        h = (int)ph;
    }
    *bw = w;
    *bh = h;
}

static esp_err_t probe_blit_marker_turnip_spi_at(int x0, int y0)
{
    uint16_t pw = 0, ph = 0;
    panel_hw_spi_fb_size(&pw, &ph);
    if (pw < PROBE_MARKER_TURNIP_RGB565_W || ph < PROBE_MARKER_TURNIP_RGB565_H) {
        return ESP_OK;
    }
    const size_t bytes =
        (size_t)PROBE_MARKER_TURNIP_RGB565_W * (size_t)PROBE_MARKER_TURNIP_RGB565_H * sizeof(uint16_t);
    uint16_t *tmp = heap_caps_malloc(bytes, MALLOC_CAP_DMA);
    ESP_RETURN_ON_FALSE(tmp, ESP_ERR_NO_MEM, TAG, "marker dma buf");
    memcpy(tmp, panel_asset_probe_marker_turnip_rgb565, bytes);
    esp_err_t err =
        panel_hw_draw_bitmap_rgb565(x0, y0, x0 + PROBE_MARKER_TURNIP_RGB565_W, y0 + PROBE_MARKER_TURNIP_RGB565_H, tmp);
    free(tmp);
    return err;
}

esp_err_t panel_hw_draw_probe_marker_corner(void)
{
    ESP_RETURN_ON_FALSE(panel_hw_panel_ready(), ESP_ERR_INVALID_STATE, TAG, "panel");
    if (panel_hw_is_spi()) {
        uint16_t pw = 0, ph = 0;
        panel_hw_spi_fb_size(&pw, &ph);
        const int x0 = (int)pw - PROBE_MARKER_TURNIP_RGB565_W;
        const int y0 = (int)ph - PROBE_MARKER_TURNIP_RGB565_H;
        return probe_blit_marker_turnip_spi_at(x0, y0);
    }
    if (panel_hw_is_i2c()) {
        return panel_hw_link_i2c_probe_marker_corner_from(panel_asset_probe_marker_turnip_mono, PROBE_MARKER_TURNIP_MONO_W,
                                                          PROBE_MARKER_TURNIP_MONO_H);
    }
    return ESP_ERR_INVALID_STATE;
}

esp_err_t panel_hw_draw_probe_marker_centred(void)
{
    ESP_RETURN_ON_FALSE(panel_hw_panel_ready(), ESP_ERR_INVALID_STATE, TAG, "panel");
    if (!panel_hw_is_spi()) {
        return ESP_OK;
    }
    uint16_t pw = 0, ph = 0;
    panel_hw_spi_fb_size(&pw, &ph);
    const int x0 = ((int)pw - PROBE_MARKER_TURNIP_RGB565_W) / 2;
    const int y0 = ((int)ph - PROBE_MARKER_TURNIP_RGB565_H) / 2;
    return probe_blit_marker_turnip_spi_at(x0, y0);
}

esp_err_t panel_hw_probe_draw_checkerboard(const test_session_t *s, unsigned cell_px)
{
    ESP_RETURN_ON_FALSE(s && panel_hw_panel_ready(), ESP_ERR_INVALID_STATE, TAG, "session/panel");
    probe_apply_session_transform(s);
    unsigned cell = cell_px ? cell_px : 16u;
    if (cell < 1u) {
        cell = 1u;
    }
    if (cell > 48u) {
        cell = 48u;
    }
    const uint16_t c0 = 0xFFFFu;
    const uint16_t c1 = 0x0000u;

    if (panel_hw_is_i2c()) {
        return panel_hw_link_i2c_checkerboard((int)cell, c0, c1);
    }
    ESP_RETURN_ON_FALSE(panel_hw_link_spi16_active(), ESP_ERR_NOT_SUPPORTED, TAG, "spi16");

    int bw = 0, bh = 0;
    probes_validation_extents(s, &bw, &bh);
    if (bw <= 0 || bh <= 0) {
        return ESP_ERR_INVALID_SIZE;
    }

    for (int y = 0; y < bh; y += (int)cell) {
        int h = (int)cell;
        if (y + h > bh) {
            h = bh - y;
        }
        for (int x = 0; x < bw; x += (int)cell) {
            int w = (int)cell;
            if (x + w > bw) {
                w = bw - x;
            }
            uint16_t c = ((((x / (int)cell) ^ (y / (int)cell)) & 1) != 0) ? c0 : c1;
            esp_err_t e = panel_hw_link_spi_fill_rect_bounds(x, y, w, h, c, bw, bh);
            if (e != ESP_OK) {
                return e;
            }
        }
    }
    return ESP_OK;
}

esp_err_t panel_hw_probe_draw_gradient(const test_session_t *s)
{
    ESP_RETURN_ON_FALSE(s && panel_hw_panel_ready(), ESP_ERR_INVALID_STATE, TAG, "session/panel");
    probe_apply_session_transform(s);
    const uint16_t top = 0x001Fu;   /* blue */
    const uint16_t bot = 0xFD20u;   /* orange */

    if (panel_hw_is_i2c()) {
        return panel_hw_link_i2c_gradient_vertical_rgb565(top, bot);
    }
    ESP_RETURN_ON_FALSE(panel_hw_link_spi16_active(), ESP_ERR_NOT_SUPPORTED, TAG, "spi16");

    int bw = 0, bh = 0;
    probes_validation_extents(s, &bw, &bh);
    if (bw <= 0 || bh <= 0) {
        return ESP_ERR_INVALID_SIZE;
    }

    int rt, gt, bt, rb, gb, bb;
    probes_rgb565_unpack(top, &rt, &gt, &bt);
    probes_rgb565_unpack(bot, &rb, &gb, &bb);
    const int denom = (bh <= 1) ? 1 : (bh - 1);

    for (int y = 0; y < bh; y++) {
        uint16_t c = probes_rgb565_pack(rt + (rb - rt) * y / denom, gt + (gb - gt) * y / denom, bt + (bb - bt) * y / denom);
        esp_err_t e = panel_hw_link_spi_fill_rect_bounds(0, y, bw, 1, c, bw, bh);
        if (e != ESP_OK) {
            return e;
        }
    }
    return ESP_OK;
}

esp_err_t panel_hw_probe_draw_turnip(const test_session_t *s, int angle_deg)
{
    ESP_RETURN_ON_FALSE(s && panel_hw_panel_ready(), ESP_ERR_INVALID_STATE, TAG, "session/panel");
    ESP_RETURN_ON_FALSE(panel_hw_link_spi16_active(), ESP_ERR_NOT_SUPPORTED, TAG, "spi16");

    probe_apply_session_transform(s);

    int bw = 0, bh = 0;
    probes_validation_extents(s, &bw, &bh);
    if (bw <= 0 || bh <= 0) {
        return ESP_ERR_INVALID_SIZE;
    }

    const int W = PROBE_MARKER_TURNIP_RGB565_W;
    const int H = PROBE_MARKER_TURNIP_RGB565_H;
    int adeg = angle_deg % 360;
    if (adeg < 0) {
        adeg += 360;
    }
    const float rad = (float)adeg * (3.14159265f / 180.0f);
    const float c = cosf(rad);
    const float sn = sinf(rad);
    const float hf = (float)W * 0.5f - 0.5f;
    const float vf = (float)H * 0.5f - 0.5f;
    const int R = (int)ceilf(sqrtf((float)(W * W + H * H)) * 0.5f) + 2;
    const int side = 2 * R + 1;
    const int cx = bw / 2;
    const int cy = bh / 2;
    const int x0 = cx - R;
    const int y0 = cy - R;
    if (bw < side || bh < side || x0 < 0 || y0 < 0 || x0 + side > bw || y0 + side > bh) {
        return probe_blit_marker_turnip_spi_at((bw - W) / 2, (bh - H) / 2);
    }

    const size_t npix = (size_t)side * (size_t)side;
    const size_t bytes = npix * sizeof(uint16_t);
    uint16_t *buf = heap_caps_malloc(bytes, MALLOC_CAP_DMA);
    ESP_RETURN_ON_FALSE(buf, ESP_ERR_NO_MEM, TAG, "turnip rot buf");

    const uint16_t bg = ui_color_bg(s);
    for (int oy = -R; oy <= R; oy++) {
        for (int ox = -R; ox <= R; ox++) {
            float sx = (float)ox * c + (float)oy * sn + hf;
            float sy = (float)(-ox) * sn + (float)oy * c + vf;
            int ix = (int)floorf(sx + 0.5f);
            int iy = (int)floorf(sy + 0.5f);
            uint16_t px = bg;
            if (ix >= 0 && ix < W && iy >= 0 && iy < H) {
                px = panel_asset_probe_marker_turnip_rgb565[(size_t)iy * (size_t)W + (size_t)ix];
            }
            buf[(size_t)(oy + R) * (size_t)side + (size_t)(ox + R)] = px;
        }
    }

    esp_err_t err = panel_hw_draw_bitmap_rgb565(x0, y0, x0 + side, y0 + side, buf);
    free(buf);
    return err;
}
