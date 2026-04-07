# The Porter’s Guide — JBG integration framework

This document is for **integrating a new SPI TFT chipset** into the JellyBean Gadgets (JBG) discovery firmware. It assumes **ESP-IDF `esp_lcd`** and the project’s **inside-out** discovery model (no magic “standard driver” offsets as substitutes for operator calibration).

Two **case studies** used throughout:

- **ST7735** — small panels, vendor init variants (tabs), full custom `JBG_CONFIG_ST7735` provisioning output in `provision_print.c`.
- **ST7789** — `esp_lcd` factory in-tree, slimmer provisioning stub today; same transport and identity seams as other ST77xx-class parts.

**v0.9 naming:** Session uses **`phys_w` / `phys_h`** (silicon extent), **`inv_en`** (display inversion enable), and **probe** terminology for discovery draws (e.g. **`panel_hw_draw_marker_probe_rgb565`**).

---

## 1. The four seams of integration

### 1.1 Transport — `esp_lcd_new_panel_*` in `panel_hw.c`

**Where:** `spi_create_panel_for_chip()` in `main/panel_hw.c`.

**What to do:**

1. Add a **`case SESSION_SPI_YOURCHIP:`** in the `switch (chip)` that calls the correct **`esp_lcd_new_panel_*`** (or vendor wrapper) with the existing `esp_lcd_panel_io_handle_t` `s_io` and `esp_lcd_panel_dev_config_t` `pc`.
2. Set **`pc.bits_per_pixel`** (16 for RGB565, 18 for ILI9488-class, etc.) and any **vendor_config** the driver requires *before* the factory call.
3. If the chip needs a **buffer size** (e.g. ILI9488), follow the same pattern as `SESSION_SPI_ILI9488`: compute `buf_sz`, pass it into the constructor.
4. **Do not** remove the shared sequence after a successful create: `esp_lcd_panel_reset` → `init` → `disp_on_off(true)` → delay → GRAM wipe for RGB565 paths.

**Case study — ST7735:** `esp_lcd_new_panel_st7735(s_io, &pc, &s_panel)`.

**Case study — ST7789:** `esp_lcd_new_panel_st7789(s_io, &pc, &s_panel)` — same `pc` shape; no extra buffer in the vendor API.

**Also update:** `spi_chip_short_name()` in the same file for logging/profile tags, and **`panel_hw_spi_init`** if the chip needs special default **invert** (see ST7735/GC9A01 branch vs others).

---

### 1.2 Identity — `PANEL_MIRROR_*` → MADCTL MX/MY in `panel_hw`

**Where:** Internal **`panel_hw_map_madctl()`** (static in `panel_hw.c`) and **`panel_hw_set_silicon_basis`**.

**Model:** Session ground truth is **`panel_mirror_t`** (`panel_probes` / Stage 3 compass). For controllers that use **ST7735/ST7789/ILI9341-class** MADCTL layout, the **socket default** maps:

- `PANEL_MIRROR_XY` → `MX | MY` (bits **0x40** and **0x80**).
- `PANEL_MIRROR_X` → `MX` only; `PANEL_MIRROR_Y` → `MY` only; `PANEL_MIRROR_NONE` → `0`.

**Case study — ST7735 / ST7789:** Both share the same `LCD_CMD_MADCTL` + `esp_lcd_panel_mirror` path in `panel_hw_set_silicon_basis()` after **`panel_hw_map_madctl()`**.

**If your chip uses different MADCTL semantics:** add a **chip-specific branch** (or a small helper) *without* changing the session enum — translate **`panel_mirror_t`** to that chip’s command bytes. Keep **`session_sync_mirror_from_silicon()`** in `session.c` as the single mapping from `silicon_mirror` to `mirror_x` / `mirror_y` for orientation XOR.

---

### 1.3 Discovery — resolution guesses in `spi_presets.c`

**Where:** `k_spi_chips[]`, per-chip `k_presets_*` arrays, and **`spi_presets_chip_gram_max()`** behavior.

**What to do:**

1. Add **`SESSION_SPI_<CHIP>`** in `session.h` (append to preserve existing numeric values if anything external depended on them).
2. In **`spi_presets.c`**, add a row to **`k_spi_chips[]`** with: short name, long label, default **PCLK**, pointer to **`k_presets_*`**, and **preset count**.
3. List **realistic WxH pairs** for the manual menu and trial flow (e.g. ST7735: 128×128 → 132×162; ST7789: 128×160, 240×240, 135×240, 240×320).

**Case study — ST7735:** Multiple presets reflect **tab / GRAM size** differences; Stage 3 + G6 refine **phys** vs visible.

**Case study — ST7789:** Presets focus on **bar vs square** modules; wrong preset often shows a **band** or **offset** until the operator picks the right WxH or custom `C`.

Also wire **`stage_panel_init.c`** manual menu order (if you group by “typical resolution”) and **`print_spi_chip_bench_tips()`** with bench hints for that controller.

---

### 1.4 Deployment — `#define` / serial provisioning in `provision_print.c`

**Where:** `provision_print_profile_dispatch()` and chip-specific printers (e.g. `provision_print_st7735_profile`).

**What to do:**

1. Add **`SESSION_SPI_<CHIP>`** cases in **`provision_print_profile_dispatch()`** — either a **dedicated** `provision_print_<chip>_profile()` with a paste-ready **`JBG_CONFIG_*`** macro, or route to **`provision_print_generic_spi_profile()`** until a full macro exists.
2. Extend **`spi_chip_name()`**, **`arduino_driver_define()`**, and **`print_target_esp_idf_lcd()`** (includes + `esp_lcd_new_panel_*` line) so menu **[1]** / **[2]** snippets match the session.
3. Keep **`stage_4_provision.c`** free of chip `if`s — it only calls **`provision_print_profile_dispatch(s)`**.

**Case study — ST7735:** Full `JBG_CONFIG_ST7735 { .w, .h, .x_gap, .y_gap, .mirror }` from session + silicon mirror bits.

**Case study — ST7789:** **`provision_print_st7789_profile()`** prints **`ST7789 Profile: [W×H …]`** plus the same **Arduino (TFT_eSPI) User_Setup.h** derived block as ST7735 (**`ST7789_DRIVER`**, **`TFT_WIDTH`/`HEIGHT`** from **`phys_w` / `phys_h`**). A full **`JBG_CONFIG_ST7789`** macro can be added when product needs parity with ST7735.

---

## 2. The “F” glyph promise

**What the operator sees:** After Stage 3 compass, a **100×100** white square at logical origin with a dark **“F”** drawn inside it.

**Implementation:** **`panel_hw_draw_marker_probe_rgb565()`** in **`panel_probes.c`** — it uses **`panel_hw_link_spi_fill_rect_rgb565`** only (after **`panel_hw_apply_gap` / `panel_hw_apply_orientation` / `panel_hw_apply_invert`** — session includes **`inv_en`** for inversion intent).

**Promise:** If the **transport stack** exposes:

- **`esp_lcd_panel_set_gap`** (or equivalent offset) via **`panel_hw_set_gap`**, and  
- **RGB565 fills** via **`esp_lcd_panel_draw_bitmap`** (wrapped as **`panel_hw_link_spi_fill_rect_rgb565`**),

then the **F marker** and **extent** probes (hline/vline on the full framebuffer) **work without** a driver-specific glyph routine. They do **not** depend on a particular controller’s init table beyond “panel can draw in session coordinates.”

The **compass** step may still use a **discovery** path (MADCTL 0x00, CASET/RASET to preset GRAM) — that is separate from the **F** path, which is **session-oriented**.

---

## 3. Handling `ESP_ERR_NOT_SUPPORTED` (ST7796 pattern)

**Problem:** You want **`session_spi_chip_t`**, **presets**, and **manual menu** entries **before** an ESP-IDF (or vendor) **`esp_lcd_new_panel_st7796`** exists in the tree.

**Pattern in `panel_hw.c`:**

```c
case SESSION_SPI_ST7796:
    /* TODO: Component Required — add vendor component; call esp_lcd_new_panel_st7796(...). */
    err = ESP_ERR_NOT_SUPPORTED;
    break;
```

**Effects:**

- **Geometry / UX:** Operators can still pick **ST7796** in **M**, see **presets** and **tips**, and plan **WxH** — useful for **documentation and bench workflow** before the driver links.
- **Init:** **`panel_hw_spi_init`** fails early with **`ESP_ERR_NOT_SUPPORTED`**; no half-initialized panel. Logs clearly indicate **missing component**, not a wiring fault.
- **Provisioning:** **`provision_print.c`** can still emit **`provision_print_generic_spi_profile()`** or **TODO** lines in the ESP-IDF snippet for **ST7796** until the real factory is wired.

**When the component arrives:** Replace the stub with the real **`esp_lcd_new_panel_*`** call, remove or narrow the TODO, and re-run the usual **Phase 0b** / **orientation** flow on hardware.

---

## Chapter 5: Exporting to Arduino

This chapter ties **JBG provisioning** (serial output at discovery complete and the interactive **provision** menu) to **Bodmer TFT_eSPI**’s **`User_Setup.h`** (or **`User_Setup_Select.h`** selection of a setup file).

### 5.1 What G9 prints for ST7735 / ST7789

After calibration, **`provision_print_st7735_profile()`** and **`provision_print_st7789_profile()`** each emit a block titled **`--- Arduino (TFT_eSPI) User_Setup.h ---`**, generated by **`provision_print_tft_espi_user_setup_h_derived()`**:

| Provisioning line | Meaning in `User_Setup.h` |
|-------------------|---------------------------|
| **`#define TFT_WIDTH`** / **`TFT_HEIGHT`** | **`phys_w`** / **`phys_h`** from the session — controller GRAM extent / resolved silicon size JBG discovered (same numbers as **`JBG_CONFIG_*`** `.w` / `.h`). |
| **`#define ST7735_DRIVER`** or **`ST7789_DRIVER`** | Selects the TFT_eSPI controller driver block; match the part on the flex. ST7735 often needs **green / red / black tab** tuning in the library if colours or origin are wrong. |
| **`#define SPI_FREQUENCY`** | Optional; bench **PCLK** when set. |
| **`#define CGRAM_OFFSET // Note: Manual offset required for X=…, Y=…`** | Emitted **only if** **`gap_col`** or **`gap_row`** is non-zero. Same values as **`esp_lcd_panel_set_gap`**; in TFT_eSPI you apply them via **viewport / offset / tab** options for your fork — the line is a reminder, not a second source of truth. |

**Pins, backlight, inversion (`inv_en`), RGB order:** the **compact** G9 block does not repeat every **`TFT_*`** pin — use provision menu **[2]** for the full **TFT_eSPI** fragment (**`board_pins.h`** values, **`TFT_INVERSION_*`**, rotation hints).

### 5.2 Interactive menu [2] vs G9 one-liner

- **Menu [2]** — full **`User_Setup.h`**-style dump: **`USER_SETUP_LOADED`**, **`TFT_MOSI`** … **`TFT_BL`**, **`SPI_FREQUENCY`**, **`CGRAM_OFFSET_COL`** / **`ROW`**, inversion, orientation comments. Use this when wiring a new Arduino sketch from scratch.
- **G9 profile** — minimal **derived** defines so you can paste under your existing **`#define …_DRIVER`** workflow without duplicating the whole file.

### 5.3 Conceptual map (ESP-IDF vs Arduino)

JBG’s **`esp_lcd`** path and TFT_eSPI both need **geometry**, **offset**, and **driver identity**; only the syntax differs. **`TFT_SDA_READ`** in TFT_eSPI is for **SPI read** (MISO) paths, **not** for column/row GRAM offset — do not confuse it with **`CGRAM_OFFSET`**.

---

## 6. Checklist (porter)

| Step | File / area | Action |
|------|-------------|--------|
| 1 | `session.h` | Add `SESSION_SPI_*` |
| 2 | `panel_hw.c` | `spi_create_panel_for_chip` + `spi_chip_short_name` + invert defaults if needed |
| 3 | `panel_hw.c` | Identity: extend MADCTL mapping if chip differs from ST77xx-style MX/MY |
| 4 | `spi_presets.c` | `k_presets_*`, `k_spi_chips[]` row |
| 5 | `stage_panel_init.c` | Manual menu line + bench tips |
| 6 | `session.c` | `spi_chip_label()` for `session_print_display_truth` |
| 7 | `provision_print.c` | Dispatch + optional `provision_print_*_profile` / generic + Arduino/ESP-IDF snippets |
| 8 | `main/CMakeLists.txt` | `idf_component_register` REQUIRES if a new `esp_lcd` vendor component is added |

---

*v0.9 — Porter’s Guide. Phase 10.1 — provisioning / probe naming alignment.*
