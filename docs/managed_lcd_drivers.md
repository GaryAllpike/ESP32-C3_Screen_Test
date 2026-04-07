# Pinned LCD drivers (ESP Component Manager)

Authoritative table: **`SPEC.md` §5.5**.

- **Manifest:** `main/idf_component.yml`
- **Lock file:** `dependencies.lock` (commit this for reproducible fetches)
- **Downloaded sources:** `managed_components/` (created by `idf.py reconfigure` or `idf.py build`)

**Exception — ST7735 (Waveshare):** This driver is **not** listed in **`main/idf_component.yml`**. The project uses a **local fork** under **`components/waveshare__esp_lcd_st7735/`** (ESP-IDF 6 `rgb_ele_order` / `LCD_RGB_ELEMENT_ORDER_*` alignment; see **`PATCHES.txt`** there). **`scripts/patch_waveshare_st7735_esp_idf6.ps1`** can patch a leftover managed copy if needed.

After cloning the repo without `managed_components/`, run from the project directory (with ESP-IDF environment):

```bash
idf.py set-target esp32c3
# or
idf.py reconfigure
```

Do not add alternate display drivers from random URLs; update **`main/idf_component.yml`** and **`SPEC.md` §5.5** together if you change packages.

**Operator UX (SPI):** Panel setup uses the drivers above in **manual** (pick chip + size, including **ST7735** presets such as **132×162**) or **try-sequence** mode — see **`README.md`** / **`SPEC.md` §4.2**–**§4.2.1** (size screen: **`wasd`** gap + **ST7735** **`[`/`]`** / **`(`/`)`** memory **WxH** nudge).

**Build / init logs:** A line like **`ili9341: LCD panel create success, version: 2.0.2`** means the **Espressif ILI9341 panel driver** finished **`esp_lcd_new_panel_ili9341()`** / init successfully; **`2.0.2`** is the **component version**, **not** a register read from the LCD controller. Do not treat it as proof of the physical part number.
