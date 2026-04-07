# PROJECT_STATE — Context bootstrap (Lead Architect handoff)

## Architectural philosophy

**Priority:** Driver integration via a **robust framework (socket pattern)**, **not** rigid spec compliance.

New controllers plug in at **known seams**: `session_spi_chip_t` + `panel_hw` factories, `spi_presets` geometry, `panel_probes` / `panel_hw_link` draws, and **`provision_print_profile_dispatch()`** for discovery-complete serial output. The UI (e.g. stages) stays **driver-agnostic**; chip-specific behavior lives behind dispatch and HAL.

## Milestone status

| Phase | Status |
|-------|--------|
| **Phase 8** (v0.9 rename, provision, probe nomenclature, `phys_w`/`phys_h`, `inv_en`) | **COMPLETE** |
| **Phase 10** (validation suite, probe-marker asset in `panel_probes.c`, automated G7 cycle + performance spin) | **COMPLETE** |

## 1. Summary

- **Project:** JellyBean Gadgets (JBG) Hardware Discovery Tool.
- **Hardware:** ST7735 SPI TFT (current bench target); framework supports additional `esp_lcd` chip enums.
- **Status:** **v0.9 (Pre-Release)** — guided **G9** runs **`stage_4_run_provision`** (probe marker + **`provision_print_profile_dispatch`**). **G7** runs **`stage_5_run_validation_loop`** (automated checker/gradient cycle + optional 360° SPI spin).
- **Architecture:** **Socket-ready** — new chipsets integrate via the four seams in **`INTEGRATION_GUIDE.md`**.

## 2. Hardware truth (ST7735 identity)

- **Physical dimensions:** 132×162 (portrait-native controller GRAM extent for this bench target).
- **Identity origin (MADCTL 0x00):** Bottom-right from the operator’s point of view.
- **Silicon Compass result (bench):** **(A) Up and Left.**
- **Verified mapping:** **MX|MY** mirror bits align logical **top-left** with physical top-left (session `PANEL_MIRROR_XY` → ST77xx-class MADCTL in `panel_hw`).

## 3. Current code architecture

- **`INTEGRATION_GUIDE.md`:** Porter’s Guide — transport, identity, discovery, deployment seams.
- **`main/panel_hw.c`:** Transport / registers; silicon basis; fills / GRAM wipe; internal **`panel_hw_map_madctl()`** for `PANEL_MIRROR_*` → MADCTL MX/MY.
- **`main/panel_probes.c`:** Discovery draws, G5/G6, Phase 0b, orientation probe, **probe marker** RGB565/mono bitmaps, validation probes (`panel_hw_probe_draw_*`).
- **`main/stage_3_probe.c`:** Silicon compass + extent flow (`stage_3_run_probe`).
- **`main/stage_4_provision.c`:** Discovery-complete panel art + **`provision_print_profile_dispatch(s)`** (no chip branches here).
- **`main/provision_print.c`:** Chip dispatch for serial provisioning / TFT_eSPI snippets.
- **`main/stage_5_validation.c`:** **`stage_5_run_validation_loop`** — automated 1 px / 8 px checkerboard + gradient (2 s steps), then Y/N prompt and **`panel_hw_probe_draw_turnip`** 360° on **Y**.
- **Session:** `phys_w`, `phys_h`, `inv_en`, gaps, `silicon_mirror` (see **`session.h`**).

## 4. Active tasks & blockers

- **Multi-chipset expansion:** `session_spi_chip_t`, `spi_presets`, `panel_hw` factories, **`provision_print_profile_dispatch`**; ST7796 may remain a transport stub until a vendor component links.
- Optional hygiene: legacy UX string cleanup in preset tables.

## 5. Anti-regression (hard constraints)

- **No** Arduino or generic “standard driver” workaround fixes in this tool’s discovery path.
- **Interactive discovery only** — **no** hard-coded panel offsets or magic tab tables in firmware as a substitute for operator calibration.
