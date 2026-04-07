# Plan: SPI try-sequence, G3 colour sanity, and per-driver RGB checks (review document)

**Status:** **Core behaviour implemented** in reference firmware (`main/stage_panel_init.c`, `main/panel_hw.c`) — Phase 0b primaries + secondaries + tri-state + format loop, **orientation-up** probe before size check, session-mapped colours for size pattern, try-sequence **resolution filter** + **`c`/`q`** pass control, **magenta** **`y`/`n`/`q`**. This document remains the **design rationale**; treat **SPEC §4.2** / **§4.2.1** + **`README.md`** as the operator-facing summary.  
**Trigger (historical):** ST7735 try-sequence showed brand graphic upside down / wrong colours; solid **green** instead of magenta is not proof of wrong driver.  
**Scope:** G3 panel setup (manual + try-sequence), **per-driver RGB + label cycle**, **tri-state screen status**, **colour-format retry**, turnip timing, SPEC alignment.

**Product decisions (locked):** Section 2.1, Section 2.3, Section 3.0, Section 3.4, Section 6, Section 9 (resolved items).

---

## 1. Problem statement

### 1.1 What the firmware does today (reference build)

- Try-sequence: optional **`W H`** filter → `panel_hw_spi_init` → **Phase 0b** (`panel_hw_spi_run_phase0b_rgb_demo` + secondaries + tri-state) → **arrow+UP** (`panel_hw_sync_orientation_up_probe` / `panel_hw_draw_orientation_up_probe`; **VanMate keys** **`R`** rotate, **`A`/`D`** mirror X, **`W`/`S`** mirror Y, **`I`** invert — **no** gap on this screen) → **size pattern** (`panel_hw_draw_spi_size_confirmation_pattern`) — **interactive:** **`wasd`** gap; **ST7735:** **`[`/`]`** / **`(`/`)`** step logical **WxH** (re-init, clamped bench range) → **secondary color test** fill → **`y`/`n`/`q`**.  
- **Phase 0b** uses **`session->spi_logical_rgb565[]`** after manual mapping; **CYAN** secondary label uses **forced black** glyphs (heuristic workaround). **~3 s** pause before secondaries when primaries **`3`** OK.  
- **Turnip** remains governed by later guided steps (centred asset — see Section 6 / firmware).  
- **Global defaults** still include **`LCD_RGB_ELEMENT_ORDER_BGR`**; **ST7735** / **GC9A01:** **`invert_on = true`** after init unless changed in session.

### 1.2 Why this misleads operators

| Observation | Wrong conclusion | Better interpretation |
|-------------|------------------|------------------------|
| Solid **green** instead of magenta | Wrong chip | Coherent fill → link likely OK; **channel order / invert** may be wrong |
| Upside-down brand graphic | Try-sequence failed | Orientation fixed in **G4** |
| Inverted colours | Bad asset | Invert tunable in G4 / format retry |

---

## 2. Goals (what “done” should mean)

### 2.1 Locked product decisions (earlier)

1. **Turnip** only after try-sequence Phases 1–3 complete + Phase 3 **Next**; **centred H&V** (not corner).  
2. **Phase 3** mandatory every try trial; minimum = explicit prompt + **Next**; optional geometry patterns.  
3. **No fast mode.**  
4. **Expert / Advanced** = well intentioned, hopefully well informed, **possibly completely ignorant** — not permission to skip validation.

### 2.2 Other goals

- Separate **link**, **colour truth**, **geometry**, **accept** in try-sequence (with RGB block addressing colour truth up front per driver).  
- Chip-specific copy and trial tables.  
- SPEC: document tri-state RGB step and format retry.

### 2.3 **Locked — per-driver RGB + label cycle (updated)**

Whenever the operator **tests a driver** — **manual** chip/geometry selection **or** each iteration of the **detect / try-sequence** — after **`panel_hw_spi_init` succeeds** for that candidate, the firmware runs **before** the rest of that trial’s flow (see Section 3.0.6):

1. **Timing:** Show **red**, then **green**, then **blue** in sequence. **Dwell at least ~1 second per primary** (1 s between colour changes is sufficient; ~3 s total minimum before the question). Implementation: `vTaskDelay` or equivalent between updates — no need for faster cycling.  
2. **On-panel:** Each step is a **solid fill** of that primary plus legible on-glass text **“RED”**, **“GREEN”**, **“BLUE”** (contrast as needed — Section 3.0.2).  
3. **Single console question** after the sequence (paraphrase allowed), e.g.:  
   - *“Is the display showing **solid single colours** with **text that matches** each colour?”*  
   - Or shorter: *“Do the three screens look like solid red, green, and blue with matching labels?”*  
4. **Tri-state answer** (one key or menu per session step):  
   - **Next / not working properly** (UI: **“Next — try another chip/size”**, key **`1`**) — snow, garbage, no image, or unusable.  
   - **Visible and clear but colours wrong** — stable picture, but labels don’t match what they see (e.g. says RED but looks green).  
   - **OK** (key **`3`**) — solids and labels **match** well enough to proceed → **secondaries** (yellow / cyan / magenta / white) after a short **look at panel** delay, then a **second** tri-state.  
5. **If not OK** (either **Next** or **colours wrong**): offer **other colour formats** — e.g. toggle **invert**, **manual mapping** interview — then **re-run** primaries (and secondaries if **`3`** again). **Next** after format retries uses **`0`** in the format submenu or **`1`** on prompts; caps see Section 9.

**Not** “once per session” — **once per driver under test** (each try-sequence trial init **and** each successful manual init path for that profile).

**I2C monochrome:** Skip RGB block or substitute mono pattern + console-only labels; SPEC.

**ILI9488 (18 bpp):** Same *intent*; dedicated fill/label path (Section 9).

---

## 3. Proposed architecture

### 3.0 **Per-driver RGB block** (G3 SPI, after each successful init)

**3.0.1 Animation**

- `panel_hw_spi_init` OK → **R** (fill + “RED”) → **delay ≥ 1 s** → **G** + “GREEN” → **delay ≥ 1 s** → **B** + “BLUE” → **delay ≥ 1 s** (optional short pause before prompt).  
- Operator reads **console** for the tri-state question only **after** all three have been shown.

**3.0.2 On-panel labels**

- Bitmap font, sprites, or high-contrast bar — same as prior plan; **labels on glass** required.

**3.0.3 Tri-state prompt + branches**

| Answer | Meaning | Next step |
|--------|---------|-----------|
| **OK** (`3`) | Solids + label match | Run **secondaries** demo; second tri-state; on full **OK** → **orientation-up** probe → **size** pattern → **Phase 1** magenta (or equivalent). |
| **Visible, colours wrong** (`2`) | Link OK, encoding wrong | **Colour-format submenu** (**invert**, **manual map**, **`0`** = next profile); re-run **Section 3.0.1** from top; loop until **OK** or **Next**. |
| **Next** (`1` / `0`) | Bad picture or wrong trial | **Abandon** this trial: `panel_hw_deinit`, next **`k_spi_trials`** entry or SPI menu — same spirit as failed init. |

**3.0.4 Colour-format submenu (product intent)**

- Shown when operator picks **“colours wrong.”**  
- Options are **implementation-defined** but must include whatever the firmware can vary **without changing chip ID** (invert toggle, `rgb_ele_order` swap if exposed, ST7735-specific MADCTL-related flags if ever surfaced).  
- After each change: **re-run R→G→B** (1 s per colour) + **same tri-state question**.

**3.0.5 Flow summary (user narrative)**

1. Operator **chooses chipset** — **manual** (`M`) or **detect / try-sequence** (`T`).  
2. On **each** driver test (`panel_hw_spi_init` success for that candidate): **R → G → B** with text, **1 s** between colours.  
3. Ask: **solid singles + text matches colours?** (paraphrase).  
4. **OK** → continue G3 for this driver (magenta phases, Phase 3, etc.).  
5. **Colours wrong** → **try other colour formats** → repeat RGB demo.  
6. **Next** → treat as failed trial, move on.

**3.0.6 Order relative to try-sequence phases**

- **Phase 0:** Init (existing).  
- **Phase 0b:** Per-driver **RGB primaries** + **tri-state** + **secondaries** + optional **format retry loop**.  
- **Orientation probe:** After full Phase 0b **OK** on **16 bpp** — **arrow + UP** with **G4-equivalent** keys (**`rwasdi`**) **before** size pattern; **preset GRAM-max** clear avoids ghost edges.  
- **Size confirmation (SPEC §4.2.1):** Nested presets + strips + inset + double frame; colours from **`spi_logical_rgb565[]`**; operator may nudge **gap** and (**ST7735**) **memory WxH** before **Enter** — **then** magenta. Skipped for **18 bpp** (e.g. ILI9488) in lockstep with RGB-labelled 0b demo skip.  
- **Phase 1:** Magenta link test (only after the above when applicable).  
- Phases **2–4** unchanged in intent (Phase 2 optional, Phase 3 mandatory Next, Phase 4 turnip).

**3.0.7 Relationship to old “Phase 2 colour sanity”**

- Per-driver **RGB + format retry** is the **primary** colour truth path.  
- Optional **Phase 2** micro-loop after magenta can remain for **fine** tweaks or be **folded** into format submenu — engineering may merge to avoid duplicate invert toggles (Section 9).

---

### 3.1 Phase 0 — Init (try-sequence trial)

- `panel_hw_spi_init` → fail → skip trial (no RGB block).

### 3.2 Phase 0b — RGB + tri-state + format retry

- See Section 3.0.

### 3.3 Phase 1 — **Link test** (magenta)

- Only after **OK** from Phase 0b.  
- **“Stable solid fill (any colour), not snow? y/n/q”** — may be **redundant** if RGB already proved link; product may **shorten** or skip Phase 1 when 0b was **OK** (Section 9).  

### 3.4 Phase 2 — **Optional** per-trial micro-loop

- As before, or merged with format submenu.

### 3.5 Phase 3 — **Mandatory confirmation**

- Prompt + **Next**; optional geometry pattern.

### 3.6 Phase 4 — **Turnip + accept**

- Turnip centred **only** after Phase 3 **Next**.

---

## 4. Data model

```text
spi_autotrial_t {
  chip, w, h, pclk_hz, hint, trial_extra
  probe_rgb565
  try_phase2_colour
  geometry_pattern_id
}
```

- **No** `rgb_sanity_done` session flag for “skip RGB” — RGB runs **every** successful init for a new driver test.  
- Optional: track **current colour format index** in session if format submenu cycles presets (session-only; SPEC excludes NVS profiles — do not persist format choice to flash unless product explicitly changes SPEC).

---

## 5. Colour and BGR (technical note)

- Primaries in RGB565; **labels** must match **operator-expected** appearance for the active `rgb_ele_order` / invert — document in SPEC which combination is “reference.”  
- **Format submenu** applies session + `panel_hw` / driver updates, then redraws RGB demo.

---

## 6. Turnip / success graphic — **decision**

| Item | Decision |
|------|----------|
| **When** | After Phases 1–3 + Phase 3 **Next** (Phase 0b must have reached **OK** earlier for this driver). |
| **Where** | Centred **H&V**. |

---

## 7. SPEC and operator docs

- **G3 SPI:** Per-driver **R/G/B** with **1 s** per screen, on-panel **RED/GREEN/BLUE**, then **tri-state**; then **secondaries** + tri-state; then **orientation-up**, **size** pattern, **magenta**.  
- **Colour wrong** path: **format** options + **repeat** demo.  
- **Next** path (**`1`** / **`0`**): abandon trial, try next profile or menu.  
- **I2C / ILI9488:** exceptions documented.  
- **Advanced** audience Section 2.1 item 4.

---

## 8. Implementation phases (for engineering schedule)

| Phase | Scope | Risk |
|-------|--------|------|
| **P0** | On-panel labels + solid fills for R/G/B | Medium |
| **P0b** | **Phase 0b:** 1 s cadence, tri-state input, format submenu + re-run loop | Medium |
| **P0c** | Wire **manual** G3 success path through same block as try-sequence | Medium |
| **P1** | Reword Phase 1 or skip when 0b OK; remove premature turnip | Low |
| **P2** | Mandatory Phase 3 + **Next** | Low |
| **P3** | Centred turnip | Medium |
| **P4** | Phase 2 merge or keep | Low |
| **P5** | Geometry patterns | Medium |
| **P6** | Driver-specific format options | Driver-dependent |

---

## 9. Open questions — **engineering / detail**

**Resolved:** RGB **per driver** (manual + each detect iteration); **1 s** between colours; tri-state after primaries and after secondaries; keys **`1` / `2` / `3`** (**Next** / colours wrong / OK); **colour-format retry** on mismatch; flow order Section 3.0.5 + orientation + size + magenta.

1. **Tri-state keys:** **Implemented:** **`1` / `2` / `3`** (+ **`r`** repeat) with explicit console legend (**Next** wording, not “Hard”).  
2. **Phase 1 magenta:** **Kept** after size check as **y/n/q** (try-sequence) or **y/n** (manual path).  
3. **Max format retries:** **Implemented** cap (**8**) with **`0`** / **`c`** escape per firmware.  
4. **ST7735 tabs**, **Phase 3 Next key**, **ILI9488**, **retry geometry**, **minimal builds** — see firmware.  
5. **Console strings** — evolve with UX plans (`docs/ux_serial_flow_plan.md`).

---

## 10. Success criteria

- Every **new** SPI driver test runs **R→G→B** with labels and **≥1 s** per colour.  
- Tri-state and format loop behave as Section 3.0.3.  
- Turnip rules preserved.  
- SPEC matches.

---

## 11. Non-goals

- Replacing **M manual** flow.  
- Silicon auto-detect.  
- LVGL-first UI.  
- Fast mode.

---

## 12. Embedded hardening (ESP32 / ESP-IDF)

This section tightens implementation so bench firmware stays **predictable** under real UART, SPI, and watchdog behaviour.

### 12.1 SPI and draw ordering

- After each **full-screen colour + labels** draw, do not advance the **1 s** dwell or the **next** primary until that draw is **committed** on the wire (see Section 12.1.1). Otherwise the operator may answer while a **previous** colour is still in the SPI queue.
- **Rule for prompts:** print the tri-state question only after the **blue** step’s last draw call has completed successfully.
- There is **no** dedicated `panel_hw_flush()` in this project; ordering relies on **`esp_lcd_panel_draw_bitmap`** return semantics plus any future helper added under `panel_hw`.

#### 12.1.1 Codebase cross-reference (`main/panel_hw.c`)

**SPI panel IO (queue depth, no async callback today)** — `spi_create_io()` builds `esp_lcd_panel_io_spi_config_t` with `trans_queue_depth = 10` and `on_color_trans_done = NULL` (completion is not signaled via callback):

```160:174:main/panel_hw.c
static esp_err_t spi_create_io(uint32_t pclk_hz)
{
    esp_lcd_panel_io_spi_config_t iocfg = {
        .cs_gpio_num = BOARD_DISPLAY_SPI_CS,
        .dc_gpio_num = BOARD_DISPLAY_SPI_DC,
        .spi_mode = 0,
        .pclk_hz = pclk_hz,
        .trans_queue_depth = 10,
        .on_color_trans_done = NULL,
        .user_ctx = NULL,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    return esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST, &iocfg, &s_io);
}
```

**Solid RGB565 fill path** — `panel_hw_fill_rgb565()` fills in **horizontal stripes** (`stripe = 16` rows), calling `esp_lcd_panel_draw_bitmap()` once per stripe with a **DMA-capable** buffer (`heap_caps_malloc(..., MALLOC_CAP_DMA)`). Each call returns `ESP_OK` or failure; the buffer is **freed only after** the full panel height is drawn. Phase 0b **must** use this (or a thin wrapper) for primaries so DMA and stripe sizing stay consistent; label draws should use the **same** completion rule.

```458:487:main/panel_hw.c
esp_err_t panel_hw_fill_rgb565(uint16_t rgb565)
{
    ESP_RETURN_ON_FALSE(s_panel && s_kind == PHW_SPI, ESP_ERR_INVALID_STATE, TAG, "spi");

    const int stripe = 16;
    size_t max_pix = (size_t)s_w * stripe;
    size_t bytes = max_pix * sizeof(uint16_t);
    uint16_t *buf = heap_caps_malloc(bytes, MALLOC_CAP_DMA);
    ESP_RETURN_ON_FALSE(buf, ESP_ERR_NO_MEM, TAG, "buf");

    for (int y = 0; y < s_h;) {
        int y2 = y + stripe;
        if (y2 > s_h) {
            y2 = s_h;
        }
        int rows = y2 - y;
        size_t npix = (size_t)s_w * rows;
        for (size_t i = 0; i < npix; i++) {
            buf[i] = rgb565;
        }
        esp_err_t e = esp_lcd_panel_draw_bitmap(s_panel, 0, y, s_w, y2, buf);
        if (e != ESP_OK) {
            free(buf);
            return e;
        }
        y = y2;
    }
    free(buf);
    return ESP_OK;
}
```

**Other draws** — `panel_hw_draw_top_marker()`, `panel_hw_draw_brand_turnip_corner()`, and I2C `panel_hw_fill_mono()` also end in `esp_lcd_panel_draw_bitmap()`. **ILI9488** uses `s_bpp == 18` in `panel_hw_spi_init()`; if primaries for 18 bpp need a different fill than the RGB565 stripe path, add a **`panel_hw`** entry point there rather than bypassing the panel handle.

**ESP-IDF expectation:** Treat **`esp_err_t` from `esp_lcd_panel_draw_bitmap`** as the gating signal for “this bitmap is done enough to reuse/free buffers” **for this codebase**. Confirm against your **ESP-IDF** version that the SPI `panel_io` path **blocks until the transaction completes** before returning (typical for this API). If a future IDF or driver queues work asynchronously, either wire **`on_color_trans_done`** in `spi_create_io()` or add an explicit **`panel_hw_wait_display_idle()`** that drains the SPI/panel_io queue before the RGB dwell timer starts.

**Public surface today (`main/panel_hw.h`):** `panel_hw_spi_init`, `panel_hw_fill_rgb565`, `panel_hw_apply_invert`, orientation/gap helpers, `panel_hw_set_backlight_pct` — no separate flush API; Phase 0b implementers extend **`panel_hw`** if a first-class wait primitive is required.

### 12.2 Task watchdog (TWDT)

- Chains of **`vTaskDelay(1000)`** normally **feed** the idle task; still verify **CONFIG_ESP_TASK_WDT** settings for the task that runs G3. Any **long** synchronous SPI flood (full-screen fills) should not run **with interrupts masked** for milliseconds without yielding.
- If RGB demo runs in a **console callback** or **low-priority** context, confirm that context is allowed to block for **3+ seconds** without tripping WDT — or **feed** the TWDT explicitly where project policy requires.

### 12.3 Backlight and power

- Ensure **backlight is on** (and **display on** after reset/init) **before** the first red frame. In this repo, **`panel_hw_spi_init()`** ends with `backlight_ledc_init()` and `panel_hw_set_backlight_pct(s)` (default **75%** if session backlight was **0**), after `esp_lcd_panel_init` / `esp_lcd_panel_disp_on_off` — so Phase 0b after a successful **`panel_hw_spi_init`** normally has **BL enabled** unless a caller overrides `s->backlight_pct` to **0** afterward.

```294:298:main/panel_hw.c
    backlight_ledc_init();
    if (s->backlight_pct == 0) {
        s->backlight_pct = 75;
    }
    panel_hw_set_backlight_pct(s);
```

- Document in SPEC if **backlight ramp** exists — first frame after BL enable may **flash**; optional **50–100 ms** settle delay after BL on.

### 12.4 UART / console interaction

- **Ignore or discard** line input that arrives **during** the R→G→B animation (or do not print the tri-state prompt until animation completes) so accidental keypresses do not select **OK** early.
- After printing the question, **flush** or **drain** stale RX if the serial layer buffers characters across the demo (product decision: one-shot read vs. clear-before-prompt).

### 12.5 Memory and ILI9488

- **18 bpp** paths may use **larger** line buffers or different APIs; the **same operator semantics** (solid primaries + labels) apply — implementation may **chunk** lines and still respect **1 s per full-screen colour** as seen by the operator.

### 12.6 Error paths

- If **`panel_hw_fill_rgb565()`**, label **`esp_lcd_panel_draw_bitmap()`**, or equivalent **fails** mid-sequence, **abort** Phase 0b with **next-trial** semantics (`panel_hw_deinit`, message, next trial) — do not ask “colours OK?” on a **partial** sequence.

---

## 13. Operational risks and mitigations

| Risk | Mitigation |
|------|------------|
| Operator presses **OK** when colours are wrong but “good enough” | Tri-state wording stresses **labels match**; format submenu remains available from **Phase 2** merge or **manual** escape if product adds it. |
| **Colour vision** — red/green discrimination | Not fixable in firmware; SPEC can note **label text** is the tie-breaker (word vs. hue). |
| **False “next”** — slow SPI or dim panel | Allow **brightness** note in docs; optional **repeat RGB** once before abandoning trial (product choice). |
| **Intermittent** snow | Section 9 recommendation on keeping some **Phase 1** link check if 0b is OK. |

---

## 14. Test matrix (suggested)

| Case | Expect |
|------|--------|
| Correct driver, default BGR/invert | **OK** on first tri-state; labels match. |
| Wrong **RGB/BGR** only | **Colours wrong** → format fix → **OK** without re-init chip. |
| Wrong chip / floating MOSI | **Next trial**; deinit; continue. |
| SPI too fast (marginal) | May flicker or tear — operator may choose **Next**; clock reduction is separate knob (existing try-sequence concerns). |
| Long UART paste during demo | No spurious branch (Section 12.4). |

---

## 15. Document history

| Revision | Notes |
|----------|--------|
| Prior | Per-driver RGB cycle at ~1 Hz step (1 s per colour), tri-state (**Next** / colours wrong / OK), **secondaries**, **other colour formats** + repeat when not OK. |
| This update | Section numbering instead of paragraph symbols; SPEC/NVS alignment; **defaults** for open questions; **embedded hardening** (SPI idle, TWDT, BL, UART); **error paths**; **operational risks**; **test matrix**. |
| Latest | **Section 12.1.1:** Cross-reference to **`main/panel_hw.c`** (`spi_create_io`, `panel_hw_fill_rgb565`, BL order in `panel_hw_spi_init`); **`panel_hw.h`** public API note; IDF **completion** verification note. |
| Doc sync | Status + §1.1 + §3.0–§3.0.6 + §7 + §9 + risks aligned with reference firmware (**secondaries**, **orientation-up**, **resolution filter**, **`c`/`q`**, **Next** wording, **ILI** log note in **`SPEC.md` §3.2.1 / **`managed_lcd_drivers.md`**). |

---

*End of plan.*
