/*
 * Internal link layer: probe/brand TUs call into panel_hw.c state without widening the public panel_hw API.
 * Include only from panel_probes.c and panel_hw.c.
 */
#pragma once

#include "esp_err.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "session.h"
#include <stdbool.h>
#include <stdint.h>

esp_lcd_panel_io_handle_t panel_hw_link_get_io(void);
esp_lcd_panel_handle_t panel_hw_link_get_panel(void);
session_spi_chip_t panel_hw_link_get_spi_chip(void);
bool panel_hw_link_spi16_active(void);
/* Active panel framebuffer size (SPI or I2C); 0 if none. */
void panel_hw_link_panel_dims(uint16_t *out_w, uint16_t *out_h);

esp_err_t panel_hw_native_clear_gram_preset_max_rgb565(const test_session_t *s, uint16_t rgb565);

esp_err_t panel_hw_link_spi_hline_rgb565(int x, int y, int len, uint16_t c);
esp_err_t panel_hw_link_spi_vline_rgb565(int x, int y, int len, uint16_t c);
esp_err_t panel_hw_link_spi_fill_rect_rgb565(int x, int y, int w, int h, uint16_t c);
esp_err_t panel_hw_link_spi_fill_rect_bounds(int x0, int y0, int w, int h, uint16_t rgb565, int bw, int bh);

esp_err_t panel_hw_link_spi_rect_outline_1px_rgb565(int x, int y, int bw, int bh, uint16_t c);
/* x1/y1 exclusive (esp_lcd_panel_draw_bitmap convention). */
esp_err_t panel_hw_link_spi_draw_bitmap(int x0, int y0, int x1, int y1, const uint16_t *rgb565);

esp_err_t panel_hw_link_i2c_batch_verification_overlay_rgb565(int W, int H);
esp_err_t panel_hw_link_i2c_top_marker_merge_band(int band_h, const uint16_t *buf, int buf_stride_px);
esp_err_t panel_hw_link_i2c_probe_marker_corner_from(const uint8_t *mono_1bpp_rowmajor, unsigned mono_w, unsigned mono_h);
esp_err_t panel_hw_link_i2c_checkerboard(int cell_px, uint16_t rgb565_a, uint16_t rgb565_b);
esp_err_t panel_hw_link_i2c_gradient_vertical_rgb565(uint16_t top_rgb565, uint16_t bot_rgb565);
