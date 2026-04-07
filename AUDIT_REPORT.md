# AUDIT_REPORT — Phase 8.10.0 (Structural / Technical Debt)

**Scope:** JellyBean Gadgets (JBG) firmware under `main/` (ESP-IDF).  
**Method:** Read-only review; **no source edits** in this phase.  
**Date note:** Line counts are non-blank physical lines as counted at audit time.

---

## 1. Structural scan

### 1.1 “Ghost logic” — `stage_3_probe.c`

| Item | Verdict |
|------|---------|
| **Dead functions** | None observed. `stage3_compass_mirror_for_choice`, `stage3_apply_silicon_compass`, and `stage_3_run_probe` are all on the live path. |
| **Obsolete variables** | **`silicon_compass`** (`'a'..'d'`) is **logically redundant** with **`silicon_mirror`** (`panel_mirror_t`): the mirror enum is fully determined by the compass letter. It remains useful as an **operator-audit trail** and for snapshot diffs, but it is duplicate state. |
| **Workflow alignment** | Silicon Compass A/B/C/D → enum → `panel_hw_session_set_silicon_mirror` is consistent; no legacy y/n aspect branch left in this file. |

### 1.2 `test_session_t` audit (`session.h`)

> **Note:** The canonical session type is **`test_session_t`**, not `session_t`.

| Field group | Role | Consolidation note |
|-------------|------|---------------------|
| **`silicon_mirror`** | Stage 3 silicon **basis** (driver-agnostic). | Canonical mirror basis after compass. |
| **`mirror_x` / `mirror_y`** | G4 **incremental** toggles; XOR with rotation table in `panel_hw_apply_orientation`. | **Dual representation** with `silicon_mirror`: both must stay in sync when Stage 3 runs (`panel_hw_session_set_silicon_mirror` sets bools). Risk: future edits could desync. **Debt:** derive bools from enum *or* store only bools and derive enum for provisioning output—pick one authority for “basis” vs “operator nudge”. |
| **`silicon_compass`** | Raw menu key. | Could be dropped if logs/provisioning used enum + string table; currently **low-cost redundancy**. |
| **`phys_w` / `phys_h`** | Preset GRAM max / silicon extent authority. | **Not redundant** with `hor_res`/`ver_res` (logical vs controller extent); keep. |
| **`silicon_extent_red_hits_right`** | Extent quiz outcome. | Independent of mirror; keep. |

**Flag — redundant / consolidatable:** `silicon_compass` ⊂ `silicon_mirror` mapping; `mirror_x`/`mirror_y` ⊂ `silicon_mirror` **at rest** after Stage 3 (but G4 still mutates bools independently).

---

## 2. Hang analysis (post-mortem) — “Magenta hang” vs deinit/reinit

**Context (from architecture history):** Phase 8.9.x moved Stage 3 silicon basis to **`panel_hw_set_silicon_basis`** (MADCTL + mirror **without** tearing down the panel) to avoid a **full `panel_hw_spi_init` mid-interview** failure mode described as a **magenta hang**. Current code still calls **`panel_hw_spi_init`** inside **`stage3_apply_silicon_compass`** after the compass answer (full deinit + bus + new panel).

**Observations from `panel_hw.c` sequence**

1. **`panel_hw_spi_init`** calls **`panel_hw_deinit()`** first → **`spi_del_panel_and_io()`** → **`spi_bus_free(SPI2_HOST)`** if SPI was up.
2. **Order:** panel del → IO del → bus free is standard, but **any in-flight SPI transaction** (DMA or queued) that outlives the handle can **stall the bus** or leave the **ILI/ST** driver in an inconsistent state until reset.
3. **Post-init:** `spi_create_panel_for_chip` performs **GRAM wipe** and delays; a **large RGB565 flood** immediately after init can **block** or **starve** other tasks if the bus was not cleanly idle—operators may perceive a **solid magenta** (or last fill color) as a “hang.”
4. **CS / DC:** If deinit runs while the panel was mid-command, **CS timing** can leave the controller **latched**; next init may mis-parse commands until **hard RST** (hardware-dependent).
5. **Backlight:** `panel_hw_deinit` drives BL duty to **0**; rapid off→on without panel sleep-out timing can combine with garbage frames (looks like solid color).

**Conclusion:** This audit **does not** prove a single root cause. **Highest-probability classes** for investigation: (a) **SPI/DMA teardown vs in-flight xfer**, (b) **controller state** after abrupt del without reset line discipline, (c) **psychological “hang”** = long synchronous GRAM wipe on weak SPI clock. **Not** flagged as a definite DMA descriptor leak without trace logs.

---

## 3. Abstraction verification — `stage_*.c` vs MADCTL / mirror

| File | `panel_mirror_t` / `silicon_mirror` | Raw MADCTL / bitmask ops |
|------|--------------------------------------|---------------------------|
| `stage_3_probe.c` | **Yes** — enum table only. | **None** in code (comment mentions MADCTL). |
| `stage_2_orientation.c` | **No** — uses **`mirror_x` / `mirror_y`** toggles only. | **None.** |
| `stage_panel_init.c` | **No** | **None** in scanned SPI flow. |
| `stage_display_adjust.c` | **No** | **None** in scan. |
| `stage_4_verify.c` | **No** | **None.** |
| `stage_patterns.c` | **No** | **None.** |

**Bleeding / dual API flag:** Stage 2 is **correct** to use bool mirrors for interactive XOR, but it is **not** “`PANEL_MIRROR` exclusive”—it bypasses `panel_mirror_t`. That is **acceptable** architecturally if `silicon_mirror` is defined as **Stage 3 basis only**; otherwise document the **two-layer model** (basis enum + G4 deltas).

**Driver coupling (macros):** **`PANEL_HW_DRIVER_MADCTL_MX` / `MY`** and **`LCD_CMD_MADCTL`** usage appear **only in `panel_hw.c`** among implementation files. **Comments** in `session.h`, `panel_hw.h`, and **provisioning strings** in `provision_print.c` mention MADCTL (not ST7735-specific macros). **Probe marker RGB565 bitmap** in **`panel_probes.c`** may contain literal `0x40u` in **binary asset data** — **not** MADCTL logic (false positive for grep).

---

## 4. Metrics (threshold flags)

| Metric | Threshold | Flagged files / notes |
|--------|-----------|------------------------|
| **File size** | > 400 lines | `panel_hw.c` (~1920), `stage_panel_init.c` (~838), `panel_probes.c` (incl. embedded marker asset), `stage_display_adjust.c` (~523), `provision_print.c` (~431), `guided_flow.c` (~388). |
| **Function length** | > 50 lines | **Not machine-verified** in this pass; **manual risk:** `panel_hw_spi_init`, large draw helpers, `spi_run_phase0b_*` / `spi_flow_*` blocks in `stage_panel_init.c`, `display_stage_g6` in `stage_display_adjust.c`. Recommend **clangd / lizard** follow-up. |
| **Coupling** | ST7735/MADCTL macros outside `panel_hw.c` | **PASS** for `#define` mirror-bit masks; **INFO** for comments / provisioning text. |
| **Nesting** | > 3 levels | **Estimated** max depth **4** in nested menus / retry loops (`stage_panel_init.c`, `stage_display_adjust.c`, stripe loops in `panel_hw.c`). Not exhaustively proven on every function. |

---

## 5. WCS summary table

**Definition (per directive):**  
**WCS = (Lines ÷ 100) + (Max Nesting × 2)**  
- **Lines:** total lines of the `.c` file (see §4).  
- **Max Nesting:** **estimated** maximum `if/for/switch` depth in that file (**4** used below where deep UI/SPI logic exists; **3** for medium files; **2** for small stage files). *Automated AST depth was not run.*

| File | Lines (≈) | Max nest (est.) | **WCS** | Major deficiency | Recommendation |
|------|-----------|-----------------|--------|-------------------|------------------|
| `panel_hw.c` | 1920 | 4 | **27.2** | Monolith: SPI, I2C, probes, G5/G6, brand, orientation in one TU | Split by **transport** (SPI vs I2C) and **feature** (discovery vs calibration patterns); keep **one** façade header. |
| `stage_panel_init.c` | 838 | 4 | **16.4** | Try-sequence + Phase 0b + manual path intertwined | Extract **SPI trial runner** and **Phase 0b FSM** into separate `.c` files with shared hooks. |
| `panel_probes.c` (data) | (see file) | 1 | **—** | Probe marker RGB565/mono in **`.rodata`** | Acceptable; regen via `tools/gen_brand_turnip.py` snippet. |
| `stage_display_adjust.c` | 523 | 4 | **13.2** | G5/G6 + backlight in one stage module | Split **G5 origin** vs **G6 extents + BL**; share `panel_apply_gap_orientation_invert`. |
| `provision_print.c` | 431 | 3 | **10.3** | Printf-heavy code generation | Template snippets or code-gen script; reduce duplication. |
| `guided_flow.c` | 388 | 3 | **9.9** | Large stage dispatch table + logic | Keep table; move **per-stage glue** next to stages. |
| `stage_3_probe.c` | 149 | 3 | **7.5** | Low debt | Optional: drop **`silicon_compass`** if provisioning uses enum only. |
| `session.h` | ~98 | — | **—** | Duplicate mirror state (see §1.2) | Document **basis vs G4 delta**; consider helper `session_sync_mirror_from_silicon()`. |

---

## 6. Anti-regression reminders (product constraints)

- **No Arduino / generic “standard driver” workarounds** in the discovery path (per project rules).
- **Interactive discovery** — avoid hard-coded panel offsets as substitutes for operator calibration.

---

*End of Phase 8.10.0 audit (read-only).*
