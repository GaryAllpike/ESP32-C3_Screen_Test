# Phase 5 — I2C OLED bring-up

## Summary

Activates the **I2C** path for **128×64** monochrome OLEDs using **ESP-IDF `esp_lcd`**: **SSD1306** (in-tree) and **SH1106** (managed component `tny-robotics__sh1106-esp-idf`).

## Task 1 — G3 driver menu (`stage_panel_init.c`)

- After identity selects **I2C**, **panel setup** offers **[1] SSD1306** or **[2] SH1106** (plus **q** skip).
- Writes **`session->i2c_driver`**, **`hor_res` / `ver_res`** = **128×64**, **`ssd1306_height`** = **64**.
- **SH1106:** **`session->gap_col = 2`** before init (132→128 column mapping); **SSD1306:** **`gap_col = 0`**.
- Does **not** run SPI Phase 0b, orientation probe, size confirmation, or SPI try-sequence.

## Task 2 — I2C init (`panel_hw.c`)

- **`panel_hw_i2c_init`** already used **`esp_lcd_new_panel_io_i2c`**, **`esp_lcd_new_panel_ssd1306`** / **`esp_lcd_new_panel_sh1106`**.
- After **`esp_lcd_panel_disp_on_off(true)`**: allocate RGB565 **shadow framebuffer**, then **`panel_hw_apply_gap`**, **`panel_hw_apply_orientation`**, **`panel_hw_apply_invert`** so **SH1106 gap** applies.

## Task 3 — RGB565 → 1 bpp bridge

- **`s_i2c_shadow`**: `uint16_t[s_w * s_h]`.
- **`panel_hw_fill_rgb565`**, **`panel_hw_fill_mono`**, **`panel_hw_draw_top_marker`**, **`panel_hw_draw_brand_turnip_corner`** (I2C) update the shadow then call **`i2c_flush_shadow_to_panel()`**.
- Conversion: **pixel lit** iff **`rgb565 != 0x0000`**; packed **row-major**, **MSB = left** (matches **`esp_lcd_panel_ssd1306`** horizontal-addressing burst length **`(y_end-y_start)*(x_end-x_start)/8`**).

## Task 4 — Handoff (`handoff_print.c`)

- **`[1] ESP-IDF`:** For **`session->bus == SESSION_BUS_I2C`**, prints **`i2c_master_bus_config_t`**, **`esp_lcd_panel_io_i2c_config_t`**, panel factory, **`esp_lcd_panel_set_gap`** with session values.
- **`[2] Arduino TFT_eSPI`:** For I2C, prints the **Adafruit_SSD1306 / U8g2** recommendation and **I2C address**.

## Guided flow

- **G4** redraw uses **`panel_hw_draw_top_marker`** on I2C as well as SPI (was blank fill only).

## Files touched

- `main/panel_hw.c` — shadow buffer, flush, I2C init tail, TOP marker, brand corner.
- `main/stage_panel_init.c` — I2C G3 menu.
- `main/stage_display_adjust.c` — G4 TOP marker on I2C.
- `main/handoff_print.c` — ESP-IDF I2C + Arduino exemption.
- `main/guided_flow.c` — overview string.
- `README.md`, `SPEC.md` (v1.14), this doc.
