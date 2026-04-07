# ESP32-C3 small display test suite

Firmware for exercising common SPI/I2C small displays on **ESP32-C3**, with a guided hookup flow and expert serial menu. **Authoritative behavior, pin map, and acceptance criteria:** [`SPEC.md`](SPEC.md) (v1.15).

## Requirements

- **ESP-IDF** (this project targets **ESP-IDF 6.x**; see workspace notes for install path).
- **Target:** `esp32c3` (`idf.py set-target esp32c3`).

## Build and flash

From this directory, with the ESP-IDF environment loaded:

```bash
idf.py set-target esp32c3
idf.py build
idf.py -p PORT flash monitor
```

## Serial console (interactive menu)

**`sdkconfig.defaults`** sets **USB Serial/JTAG** as the primary console (`CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y`). Use the **same USB cable** as flash/monitor; **`printf`** / **`getchar()`** go over USB. This avoids putting **UART0** on **GPIO20/21**, which the default SPI pin map uses for **MOSI/RST** (see **`SPEC.md` Appendix B** and **`main/board_pins.h`**).

**Terminal settings:** Single-key menus **do not** echo keys from firmware — turn **local echo off** on the host so prompts stay clean. **Line entry** (hex I2C address, custom **W×H**, try-sequence **`W H`** filter) uses a **blocking** console read; firmware **does not** echo those characters — use **host local echo** if you need to see what you type, or rely on **`!`** / **`@`** as documented in **SPEC §7**.

**Firmware defaults (SPI peak check, final guided step):** Coarse PCLK ladder **20 → 26 → 40 → 53 → 80 MHz** (skips rungs already at or below the current clock). Each candidate step runs a short alternating fill **stress** (~1 s) before **y/n**; see `display_stage_g8()` in `main/stage_patterns.c`.

### Keys (firmware) — Phase 2 layout

Normative detail: **SPEC §4.2.1**, **§7.3**, **§8**, **§9**, **§10**. Summary:

- **Letter keys are case-insensitive** (`R` and `r` are the same).
- **Global spatial / gap (where applicable):** **`wasd`** — **a**/**d** column gap −/+ , **w**/**s** row gap −/+ (same idea as old **L/R/U/V**). **Exception:** on **orientation-only** screens (arrow-before-size, G4 Orientation), **`wasd`** are **not** gap keys — see below.
- **Global nav in stage submenus:** **comma** reverts the current submenu to values when you opened it; **period** or **Enter** confirms / saves (where the on-screen **NAV** block says so).
- **Orientation-style screens (arrow-before-size, G4 Orientation, SPI RGB565):** VanMate-style map — **`R`** rotate +90°, **`A`** / **`D`** each **toggle** mirror X, **`W`** / **`S`** each **toggle** mirror Y, **`I`** invert; **comma** revert; **Enter** or **.** done. **Gap** (column/row offset) is **not** adjusted here — use the **size-check** screen or **Picture alignment** after rotation is locked. Firmware calls **`panel_hw_apply_orientation`** + **`panel_hw_draw_orientation_up_probe`** each refresh so MADCTL and the arrow stay in sync. (**Note:** on the **guided shell**, **`R`** is **restart wiring** — same glyph, different layer; see on-device **NAV** text.)
- **Main guided shell:** **.** or **Enter** next step; **comma** previous (from **panel setup**, **comma** revisits wiring), **R** restart from wiring review, **E** Advanced menu, **O** print config / provision, **G** short “where you are” text. **`!`** full restart from hookup; **`@`** restore last known-good display snapshot.
- After identity, an **overview** lists next steps; **Enter** continues straight into **panel setup** (SPI **M**/**T** and I2C driver picks apply there) or **A** opens Advanced. From that overview, Advanced **option 1** (return to main steps) also continues straight into **panel setup** — the overview is **not** shown again.
- **Panel setup (I2C):** **1** = **SSD1306** 128×64, **2** = **SH1106** 128×64 (default **column gap +2** for SH1106). Skips SPI Phase 0b / try-sequence. OLED draws use an internal RGB565→1 bpp bridge before `esp_lcd` blit.
- **Panel setup (SPI):** **M** = pick chip from flex/PCB marking (then preset or custom size). **T** = try-sequence: optional **`W H`** filter (e.g. `128 160`) or **Enter** for all trials; order is **smallest pixel area first** (see **SPEC §4.2**). After each successful init: **color alignment** (primaries + secondaries + tri-state — firmware may still label this Phase 0b internally; **`1`** = next trial, **`2`** = adjust colour format, **`3`** OK), **Step 2** arrow+**UP** orientation (**`R`** / **`A`**/**`D`** / **`W`**/**`S`** / **`I`** / **comma** / **.** / **Enter** — see above; **no** gap nudge on this screen), **size** pattern (see below), then **secondary color test** (solid magenta-mix fill) — **`y`** accept, **`n`** next trial, **`q`** stop pass (SPI menu). End of pass: **`c`** cycle from first trial, **`q`** SPI menu.
- **Size check (16 bpp SPI, on-screen):** **wasd** = column/row gap. **comma** reverts **gap** to values at entry; **Enter** or **.** continues. **ST7735 only:** **`[`** / **`]`** = memory width −1/+1, **`(`** / **`)`** = memory height −1/+1 (re-inits panel; session keeps gap, rotation, colours). **`!`** / **`@`** work like other single-key menus.
- **Origin / gap (G5):** **SPI RGB565** — colour **ballpark** at GRAM (0,0) (**R**/**G**/**B**/**M** bands), then **yellow origin probe** + **wasd**; **I2C** / **18 bpp SPI** — classic **wasd** gap only.
- **Extents & backlight (G6):** **SPI RGB565** — nested **M/C/G/R** rectangles, **ballpark** quiz for **hor_res**/**ver_res**, **extent probe** (**A**/**D** width, **W**/**S** height), then **W/S** backlight (same rules as below). **18 bpp / I2C** — backlight or N/A.
- **Backlight (SPI TFT):** **w** brighter, **s** dimmer (5% steps); **comma** revert to level when you opened the screen; **Enter** or **.** done.

Advanced menu **option 2** prints the same config block as **O**.

**Operator labels vs docs:** firmware shows plain step names on the serial console (no **G3**-style IDs in UI); [`SPEC.md`](SPEC.md) still uses internal stage IDs (G1…G9) for traceability — behavior matches either way.

## Project layout (quick)

| Item | Role |
|------|------|
| **`SPEC.md`** | Product spec, pin manifest (**Appendix B**), UX and safety rules |
| **`main/board_pins.h`** | Compile-time GPIO map (must match **Appendix B**) |
| **`main/idf_component.yml`** | ESP Component Manager deps (**ST7735** is **not** here — see below) |
| **`components/waveshare__esp_lcd_st7735/`** | Local **ST7735** driver (ESP-IDF 6 compatibility); see **`PATCHES.txt`** |
| **`CMakeLists.txt`** | **`MINIMAL_BUILD`**, **`unset(COMPONENTS CACHE)`** for a minimal component set |
| **`docs/`** | Extra notes (e.g. managed drivers, hardware photos) |

## Documentation

- **Hardware / board photos:** [`docs/hardware/README.md`](docs/hardware/README.md)
- **Registry vs local ST7735:** [`docs/managed_lcd_drivers.md`](docs/managed_lcd_drivers.md)
- **Phase 5 I2C OLED bring-up (implementation notes):** [`docs/PR_phase5_i2c_oled.md`](docs/PR_phase5_i2c_oled.md)
- **Phase 6 geometry (G5/G6 ballpark & probes):** [`docs/PR_phase6_geometry.md`](docs/PR_phase6_geometry.md)
