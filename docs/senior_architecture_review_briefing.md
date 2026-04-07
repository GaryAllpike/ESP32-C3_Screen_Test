# Senior design architecture review — briefing document

**Project:** ESP32-C3 small display test suite (bench firmware)  
**Repository root:** `esp32-c3_screen_test/` (paths below are relative to this root unless noted)  
**Audience:** Senior design architect performing a deep technical and product review  
**Constraints for this briefing:** No source code excerpts; file names and line numbers are used as pointers only.

---

## 1. Purpose of this document

This briefing orients a senior reviewer to:

- **Product intent** and where it is normatively captured  
- **Documentation** that should be read alongside the code  
- **Software architecture** (modules, data flow, build boundaries)  
- **Operational / safety** assumptions (hardware, console, recovery)  
- **Suggested review axes** (consistency, maintainability, failure modes, UX)

It does **not** replace reading `SPEC.md` or the referenced design notes; it maps *where* to look.

---

## 2. Complete bibliography (documents this briefing references)

| Document | Path (from repository root) |
|----------|-----------------------------|
| Product specification (normative) | `SPEC.md` |
| Operator quick reference (build, console, keys) | `README.md` |
| Pinned vs local LCD drivers | `docs/managed_lcd_drivers.md` |
| Hardware photos / board index | `docs/hardware/README.md` |
| SPI try-sequence, Phase 0b rationale | `docs/plan_spi_try_sequence_subtests.md` |
| G5 alignment pattern vs G3 size pattern | `docs/plan_g5_alignment_pattern.md` |
| Serial UX overhaul plan (historical / gap analysis) | `docs/ux_serial_flow_plan.md` |
| Prior architecture review (issues, some marked FIXED) | `docs/architecture_review.md` |
| Junior console review notes | `docs/senior_review_junior_firmware_console.md` |
| ST7735 vendor fork patch notes | `components/waveshare__esp_lcd_st7735/PATCHES.txt` |
| Root CMake (minimal build flag) | `CMakeLists.txt` |
| Main component registration | `main/CMakeLists.txt` |
| Component Manager manifest | `main/idf_component.yml` |
| Lock file for managed deps | `dependencies.lock` (if present after configure) |
| Default Kconfig / console | `sdkconfig.defaults` |

**Authoritative pairing:** `SPEC.md` + `main/board_pins.h` for GPIO manifest (`SPEC.md` Appendix B). Any change to pins should touch both in one change set (`SPEC.md` revision rule).

---

## 3. Product summary (what success looks like)

From `SPEC.md` §1: firmware exercises **small LCD/OLED** modules on **ESP32-C3** using **ESP-IDF** and **`esp_lcd`**, with **human visual confirmation** and **interactive serial**. Goals include wiring verification, controller compatibility, geometry and offsets, orientation, colour encoding, and (where applicable) **peak stable SPI clock**. **Session-only** configuration — **no NVS** persistence of profiles (`SPEC.md` §1.2).

Target board narrative: **Tenstar Robot ESP32-C3 Super Mini** (+ expansion board) — `SPEC.md` §2.1, `docs/hardware/README.md`.

---

## 4. Build system and dependency boundary

### 4.1 Minimal IDF build

- `CMakeLists.txt` enables **`MINIMAL_BUILD`** and clears cached **`COMPONENTS`** so the full IDF tree is not forced into the build (lines 4–10).  
- Project name: `esp32-c3-screen-test` (line 11).

### 4.2 Main component

- `main/CMakeLists.txt` lists every translation unit compiled into the app and **`REQUIRES`** for drivers: GPIO, I2C, SPI, LEDC, `esp_lcd`, and vendor panel components (lines 2–32).  
- **ST7735** is **`waveshare__esp_lcd_st7735`** from **local** `components/` — **not** from the Component Manager (`main/idf_component.yml` comment; `docs/managed_lcd_drivers.md`).  
- Other SPI panels: **`espressif__esp_lcd_ili9341`**, **`espressif__esp_lcd_gc9a01`**, **`atanisoft__esp_lcd_ili9488`**; I2C: **`tny-robotics__sh1106-esp-idf`** (plus core SSD1306 via `esp_lcd` as wired in code — see `panel_hw.c` / `SPEC.md` §5.5).

### 4.3 Third-party trees

- **`managed_components/`** — downloaded registry packages; treat as **read-only** for product review except version pins in `main/idf_component.yml`.  
- **`components/waveshare__esp_lcd_st7735/`** — vendored fork; integration rationale in `PATCHES.txt` and `SPEC.md` §5.5.

---

## 5. Runtime architecture (high level)

### 5.1 Execution model

- **Single-app thread:** `app_main` (`main/app_main.c`, approx. lines 10–17) calls `appshell_run()` in a **forever** outer loop.  
- **Unbuffered stdio:** `setvbuf` on `stdout` / `stdin` in `app_main` (lines 12–13) — documented in `README.md` for prompt flush and line echo behaviour.  
- **No separate GUI task:** all UX is **blocking serial menus** (`main/serial_menu.c`) and **FreeRTOS delays** inside draw helpers.

### 5.2 Boot chain (reference order)

Documented in `README.md`, `SPEC.md` §6–§7, and implemented in `main/appshell.c` (`appshell_run`, approx. lines 16–53):

1. `session_init`, invalidate display recovery snapshot  
2. Hookup instructions (`hookup_print.c`)  
3. `safe_idle_configure_display_pins` (`safe_idle.c`) — benign GPIO state before panel init  
4. `serial_wait_enter_hooks` — **Enter** / **`!`** / **`@`**  
5. `identity_probe_transport` (`identity.c`) — I2C vs SPI selection  
6. `guided_show_overview_and_wait` (`guided_flow.c`) — **Enter** vs **Advanced**  
7. `guided_flow_run` — staged display setup and tests  

Full restart **`!`** loops the outer `appshell_run` iteration; **`@`** restores from RAM snapshot (`display_recovery.c`) without necessarily rebooting.

---

## 6. Session state and configuration model

### 6.1 `test_session_t`

Defined in `main/session.h` (struct and enums, approx. lines 9–61). Carries:

- Transport: bus type, I2C address, transport override (force SPI/I2C)  
- Panel profile: chip enum, geometry, gap, rotation, mirror, invert, RGB element order flag  
- **Logical colour mapping:** `spi_logical_rgb565[3]` (Phase 0b + manual interview)  
- Backlight %, SPI PCLK, peak SPI measurement  

**API:** `session_init`, `session_reset_display_fields` (clears display subset, preserves transport fields per comment at lines 65–66), `session_print_display_truth` (serial “truth” snapshots for operators).

### 6.2 Design note for reviewers

Several UX flows **re-init** the SPI panel (e.g. ST7735 memory width/height nudge in `stage_panel_init.c`). **`panel_hw_spi_init`** (`main/panel_hw.c`, approx. lines 275–347) must **preserve** operator-tuned session flags when the **chip family** is unchanged (e.g. invert default applied only on chip change — see comments and logic near lines 280–282 and 323–331). This is a **session vs driver defaults** boundary worth auditing for similar regressions.

---

## 7. Display hardware abstraction (`panel_hw`)

### 7.1 Role

`main/panel_hw.c` / `main/panel_hw.h` centralize:

- SPI2 bus, panel IO, panel handle, I2C master bus  
- Init per chip (`panel_hw_spi_init`, `panel_hw_i2c_init`), deinit ordering (`panel_hw_deinit`)  
- Applying session transforms: gap, orientation (MADCTL path), invert  
- Fill and pattern primitives used by G3/G4/G5/G8 (declarations in `panel_hw.h`, lines 8–52)

### 7.2 SPI init contract

- Validates geometry bounds (approx. line 278 in `panel_hw.c`).  
- Computes `max_transfer_sz` from WxH (approx. 282–285).  
- After successful create: sets session `hor_res`, `ver_res`, `profile_tag`, `panel_ready` (approx. 338–342).  
- Backlight via LEDC (`backlight_ledc_init` and related static state — see `panel_hw.c` header region and LEDC macros near lines 54–57).

### 7.3 Review hooks

- **ILI9488** 18 bpp path vs 16 bpp — pattern skips and buffer sizing (`SPEC.md` §4.2.1; `panel_hw.c` ILI9488 branch in `spi_create_panel_for_chip`, approx. lines 237–244).  
- **Monolithic vs striped** fills for orientation probe when `swap_xy` — search `panel_hw.c` for `panel_hw_fill_rgb565_monolithic` / orientation probe (architectural trade: RAM vs artifacts).

---

## 8. Guided flow and stages

### 8.1 Stage dispatch

`main/guided_flow.c`:

- Internal enum **G1–G9** (`guided_stage_t`, approx. lines 24–35); **G3–G8** map to `display_stage_g*` functions (table `k_stage_run`, approx. lines 38–47).  
- **Expert / Advanced** submenu (`expert_menu`, approx. lines 83+) — transport overrides, handoff print, resume stage.  
- Operator strings: `guided_ui_strings.c` / `guided_ui_strings.h` (`guided_stage_meta_t`).

### 8.2 Stage implementation files

| Stage | Responsibility | Primary file |
|-------|------------------|--------------|
| G3 | Panel setup: SPI manual / try sequence / I2C driver pick; Phase 0b; orientation-before-size; size check loop; magenta | `main/stage_panel_init.c` |
| G4 | Orientation + TOP marker | `main/stage_display_adjust.c` |
| G5 | Picture alignment / gap | `main/stage_display_adjust.c` |
| G6–G8 | Backlight, patterns, peak SPI | `main/stage_patterns.c` |

**Key definition header:** `main/display_stages.h` — valid key sets for menus (**Phase 2 + 8.x:** `wasd` gap on size/alignment screens; **`rwasdi`** orientation on G4 / G3 Step 2; **comma**/**`.`**/**Enter** nav; ST7735 size screen `STAGE_KEYS_G3_SIZE_CHECK_ST7735` includes `[` `]` `(` `)` for memory WxH).

### 8.3 SPI preset data

- `main/spi_presets.c` / `spi_presets.h` — per-chip geometry presets and chip table for manual menu and try-sequence ordering.  
- Try-sequence trial array and resolution filter: `stage_panel_init.c` (search `k_spi_trials`, `spi_try_autosequence`).

**Architect note:** Operator-facing geometry discovery (gap, ST7735 GRAM WxH nudge, presets including 130×160 and 132×162) is specified in `SPEC.md` §4.2–§4.2.1 and `README.md`; implementation should stay aligned with those sections.

---

## 9. Serial I/O and UX

### 9.1 Input layer

- `main/serial_menu.c` / `serial_menu.h` — `serial_read_menu_choice`, line-oriented reads, global hooks **`!`** / **`@`** (shared helper pattern per comments near lines 28–39).  
- `main/console_text.c` — word wrap for long operator prose.

### 9.2 Key-semantics collisions

**Updated (Phase 2 + 8.x UX):** Orientation submenus (**G4**, G3 arrow-before-size) use **`R`** rotate, **`W`/`S`** mirror Y, **`A`/`D`** mirror X, **`I`** invert (**`rwasdi`** + nav). Picture alignment / size-check use **`wasd`** for gap (**`s`** = row down). **`R`** on the **guided shell** is **Restart**, not rotate — same glyph, different menu. **`O`** = print config on the guided shell. Prompts must match **`serial_read_menu_choice`** valid sets (`display_stages.h`, `guided_flow.c`).

---

## 10. Identity and transport

- `main/identity.c` / `identity.h` — I2C scan, classification, SPI fallback; respects `session.transport_override`.  
- Prior review of probe error handling: `docs/architecture_review.md` §2.1 (marked FIXED); architect should confirm behaviour matches current `identity.c`.

---

## 11. Safety, recovery, and handoff

- **`safe_idle`:** `main/safe_idle.c` — pin-safe state before display bring-up (`SPEC.md` / `architecture_review.md` praise this pattern).  
- **`display_recovery`:** `main/display_recovery.c` — `memcpy` snapshot of `test_session_t`; restore re-inits panel and reapplies gap/orientation/invert; preserves `transport_override` explicitly (see comments near lines 43–46).  
- **Handoff:** `main/handoff_print.c` / `handoff_print.h` — printable session summary (`SPEC.md` §13).

---

## 12. Assets and branding

- `main/brand_turnip_assets.c`, `main/brand_turnip.h` — embedded graphics; placement rules tied to guided stages (`SPEC.md` / `plan_spi_try_sequence_subtests.md`).  
- Colour constants: `main/ui_colors.h` (probe fills, UI semantic colours).

---

## 13. Pin manifest

- **`main/board_pins.h`** — authoritative GPIO defines; must match `SPEC.md` Appendix B.  
- Printed hookup text: `main/hookup_print.c`.

---

## 14. Suggested review axes (checklist)

1. **Spec ↔ code traceability:** For each `SPEC.md` acceptance row (§18), is there an obvious code owner? Any drift in version strings (e.g. `app_main.c` header still cites older SPEC version at line 2 — worth normalizing)?  
2. **Session invariants:** After every `panel_hw_spi_init` and `panel_hw_deinit` pairing, are session fields consistent with operator expectations (invert, logical RGB565, gap)?  
3. **Resource limits:** Largest SPI transfers, heap use for monolithic fills, stack depth in `console_text` / menus.  
4. **Error paths:** I2C bus faults, SPI init failure, partial panel create — leak-free and operator-informative (`docs/architecture_review.md` §2.2 historical panel leak fix).  
5. **Key map completeness:** Each screen documents only keys accepted by `serial_read_menu_choice` for that screen (`display_stages.h` vs prompts in `stage_*.c`).  
6. **MINIMAL_BUILD assumptions:** Any indirect include or Kconfig dependency not reflected in `main/CMakeLists.txt` `REQUIRES`.  
7. **Vendor driver boundary:** Local ST7735 vs managed components — who owns COLMOD/MADCTL conflicts (vendor log warnings vs session mapping).  
8. **Long-term UX:** `docs/ux_serial_flow_plan.md` lists gaps (metadata table, 60-col standard); assess whether duplication between `SPEC.md`, `README.md`, and on-device strings is acceptable technical debt.  
9. **Testability:** No automated on-target tests in-tree; review whether critical flows are structurally testable (e.g. session serialization) or forever manual.

---

## 15. Explicit non-goals (product)

From `SPEC.md` §1.2: no touch, no screenshot pipeline, no NVS profiles, no auxiliary buttons — architect should flag scope creep if new subsystems appear without spec update.

---

## 16. Document revision

| Version | Date | Notes |
|---------|------|-------|
| 1.0 | 2026-04-05 | Initial senior review briefing; bibliography complete; no source excerpts |

*End of briefing.*
