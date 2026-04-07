/* Version: 0.9 (Pre-Release) | JBG Discovery Framework */
#pragma once

#include "session.h"
#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

void panel_hw_deinit(void);
void panel_hw_safe_swap_pause(void);
esp_err_t panel_hw_reinit_from_session(test_session_t *s);

bool panel_hw_panel_ready(void);
bool panel_hw_is_spi(void);
bool panel_hw_is_i2c(void);

esp_err_t panel_hw_spi_init(test_session_t *s, session_spi_chip_t chip, uint16_t w, uint16_t h, uint32_t pclk_hz);
esp_err_t panel_hw_i2c_init(test_session_t *s, session_i2c_driver_t drv, uint8_t addr_7bit, uint8_t ssd1306_height);
esp_err_t panel_hw_spi_set_pclk(test_session_t *s, uint32_t pclk_hz);

void panel_hw_spi_wipe_controller_gram_rgb565(const test_session_t *s, uint16_t rgb565);
esp_err_t panel_hw_spi_paint_discovery_gram_rgb565(const test_session_t *s, uint16_t rgb565);

void panel_hw_apply_gap(const test_session_t *s);
void panel_hw_apply_orientation(const test_session_t *s);
void panel_hw_apply_invert(const test_session_t *s);
/* Push orientation / invert to controller and refresh session->madctl (SPI RGB565); I2C leaves madctl 0. */
void panel_hw_set_orientation(test_session_t *s);
void panel_hw_set_inversion(test_session_t *s);
esp_err_t panel_hw_set_gap(int16_t col, int16_t row);
void panel_hw_set_mirror(bool mirror_x, bool mirror_y);
/* SPI: driver gap registers to (0,0); session gap unchanged — before native-GRAM silicon probe. */
void panel_hw_spi_clear_hardware_gap(void);
void panel_hw_session_set_silicon_mirror(test_session_t *s, panel_mirror_t m);
void panel_hw_set_silicon_basis(const test_session_t *s);
void panel_hw_set_backlight_pct(const test_session_t *s);

esp_err_t panel_hw_fill_rgb565(uint16_t rgb565);
/* SPI RGB565: atomic CASET/RASET hygiene; pixels may be const FLASH (caller copies to DMA if needed). */
esp_err_t panel_hw_draw_bitmap_rgb565(int x0, int y0, int x1, int y1, const uint16_t *rgb565);
void panel_hw_nuclear_clear(test_session_t *s);
esp_err_t panel_hw_fill_mono(uint8_t fill_byte);
esp_err_t panel_hw_draw_top_marker(const test_session_t *s);
esp_err_t panel_hw_draw_orientation_up_probe(const test_session_t *s);
void panel_hw_sync_orientation_up_probe(const test_session_t *s);
/* Stage 3: 100×100 at native (0,0), axis lines to GRAM edge; caller restores gap/orient/invert. */
esp_err_t panel_hw_draw_silicon_probe(const test_session_t *s);
/* 100×100 + F marker at logical (0,0) under session transform. */
esp_err_t panel_hw_draw_marker_probe_rgb565(const test_session_t *s);
esp_err_t panel_hw_draw_stage3_extent_probe_rgb565(const test_session_t *s);
esp_err_t panel_hw_draw_rect_raw(const test_session_t *s, int x0, int y0, int x1_inclusive, int y1_inclusive,
                                 uint16_t rgb565);
esp_err_t panel_hw_draw_g5_alignment_pattern(const test_session_t *s);
esp_err_t panel_hw_draw_g5_origin_ballpark_rgb565(const test_session_t *s);
esp_err_t panel_hw_draw_g5_origin_probe_rgb565(const test_session_t *s);
esp_err_t panel_hw_draw_g6_extents_ballpark_rgb565(const test_session_t *s);
esp_err_t panel_hw_draw_g6_extents_probe_rgb565(const test_session_t *s, uint16_t log_w, uint16_t log_h);
esp_err_t panel_hw_draw_batch_verification_overlay_rgb565(const test_session_t *s);

esp_err_t panel_hw_spi_run_phase0b_rgb_demo(const test_session_t *s);
esp_err_t panel_hw_spi_run_phase0b_secondaries_demo(const test_session_t *s);

esp_err_t panel_hw_draw_probe_marker_corner(void);
esp_err_t panel_hw_draw_probe_marker_centred(void);
/* Validation suite (SPI RGB565 unless noted): respects session gap, orientation, inv_en. */
esp_err_t panel_hw_probe_draw_checkerboard(const test_session_t *s, unsigned cell_px);
esp_err_t panel_hw_probe_draw_gradient(const test_session_t *s);
esp_err_t panel_hw_probe_draw_turnip(const test_session_t *s, int angle_deg);

uint32_t panel_hw_spi_pclk_hz(void);
uint8_t panel_hw_bits_per_pixel(void);
void panel_hw_spi_fb_size(uint16_t *out_w, uint16_t *out_h);

/* SPI ST77xx-class MADCTL byte implied by session (silicon mirror + RGB order); no panel I/O. I2C → 0. */
uint8_t panel_hw_truth_madctl_byte(const test_session_t *s);
