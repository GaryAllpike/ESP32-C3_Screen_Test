# Phase 6 — Ballpark & probe geometry (G5 / G6)

## G5 — Origin (`gap_col`, `gap_row`)

**SPI RGB565 only** for the colour steps; I2C and SPI 18 bpp keep the previous **wasd** gap loop with the legacy nested-line pattern on SPI 18 bpp.

1. **Ballpark:** `panel_hw_draw_g5_origin_ballpark_rgb565` forces **gap 0**, draws 20 px-thick **R / G / B / M** segments along the **top** (by x) and **left** (by y) from GRAM (0,0), then restores session gap.
2. Serial: top bezel **R G B M** → `gap_row` ∈ {0,20,40,60}; left bezel → `gap_col` same.
3. **Origin probe:** Black fill + **2 px** yellow crosshairs at logical origin; **wasd** as before; **,** reverts gap to start of this step; **Enter** / **.** saves.

## G6 — Extents (`hor_res`, `ver_res`) + backlight

**SPI RGB565:** nested fills from logical (0,0): **240×320 M**, **135×240 C**, **128×160 G**, **128×128 R**. Serial **M C G R** for **right** then **bottom** bezel → width/height lookup; clamp to panel FB from `panel_hw_spi_fb_size`. **Extent probe:** 2 px yellow at bottom-right inside logical size; **A/D** = `hor_res`, **W/S** = `ver_res`; **,** reverts to start of this step. Then **backlight** (same as old G6).

**SPI 18 bpp / I2C:** extents N/A; backlight only where SPI.

## Files

- `main/panel_hw.c` — draw helpers; `main/panel_hw.h` — declarations.
- `main/stage_display_adjust.c` — G5/G6 UX.
- `main/display_stages.h` — `STAGE_KEYS_G6_EXTENTS`.
- `main/guided_ui_strings.c` — stage titles/blurbs.
