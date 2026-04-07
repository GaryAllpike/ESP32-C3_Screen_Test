# Plan: G5 alignment pattern + geometry / size confirmation (review document)

**Status:** **G3 size confirmation (Section A)** is **implemented** in **`panel_hw_draw_spi_size_confirmation_pattern()`** — full framebuffer clear, nested presets from **`spi_presets`**, **TOP/LEFT** strips and colours from **`session->spi_logical_rgb565[]`**, inset **blue** outline, **double** outer frame at logical **WxH**. On the **size-check** wait loop (**`stage_panel_init.c`**), the operator may nudge **gap** (**`wasd`**) and, for **ST7735**, step **logical memory WxH** (**`[`/`]`**, **`(`/`)`**) before **Enter** or **`.`** — see **`SPEC.md` §4.2.1** / **`README.md`**. **Orientation-before-size** uses **arrow+UP** and **`rwasdi`** keys (**no** gap there). **G4 vs G5 preview split** is implemented (**`panel_hw_sync_orientation_up_probe`** / TOP marker vs **`panel_hw_draw_g5_alignment_pattern`** in `stage_display_adjust.c`). This document still describes **design intent** and **Tier 3** (safe label band) follow-ups; **`SPEC.md` §4.2.1** is normative for order (Phase 0b → orientation-up → size → secondary color test).

**Goals (two related patterns):**

1. **G5 (alignment / gap):** Give the operator a **reference image** for **single-pixel** **wasd** gap tuning: nested **1-pixel** borders (existing plan below).
2. **Geometry / size confirmation (G3 or post-init SPI):** Let the operator **visually confirm or correct** chosen **width × height** using **nested rectangles** whose **steps match only common screen sizes** from the firmware preset table; **manual WxH** when no preset fits.

**Product decisions (pending lock):** Section 5; **defaults / recommendations** inline there and in Section 9.

**Locked — G4 / G5 independence:** **Orientation (G4)** and **alignment / gap (G5)** use **separate** on-panel preview paths. Changing G5’s pattern **must not** change G4’s **TOP marker** behaviour, and vice versa. Shared code may still call **`panel_hw_apply_gap`**, **`panel_hw_apply_orientation`**, and **`panel_hw_apply_invert`** in a common helper; only the **final draw** is stage-specific (Section 2.1, Section 3.1).

**Coordinate convention (locked for all new patterns):** Use the **same origin as the rest of this firmware and ESP-LCD** — **logical (0,0) = top-left** of the drawable framebuffer, **+X** to the right, **+Y** downward. Do **not** use a bottom-left GL-style origin; console copy should say **“top edge = smallest Y”** and **“left edge = smallest X”** so operators are not confused.

---

## 0. Implementation tiers and scope control

Deliver in **separate milestones** so reviews stay small and behaviour stays predictable. An implementer (human or agent) should **not** merge unrelated work.

| Tier | What | Primary sections | When to ship |
|------|------|------------------|--------------|
| **1** | **G4 vs G5 preview split** + **G5 nested 1 px border pattern** (SPI RGB565 path) | §1, §2.1, §3, §7, §10 | First firmware PR for this plan |
| **2** | **Size confirmation pattern (Section A)** + G3 hook after **`panel_hw_spi_init`** succeeds | §A, §A.2 | **Shipped** in reference firmware (`stage_panel_init.c` + `panel_hw.c`) |
| **3** | **Phase 0b safe label band** (labels not centred in full H when visible area is smaller) | §A.4, `plan_spi_try_sequence_subtests.md` | May track Tier 2 or land separately |

**Tier 1 is sufficient** to resolve “G5 still shows TOP marker” and meet the **locked** G4/G5 independence rule. **Do not** block Tier 1 on Tier 2.

### 0.1 Files likely touched (Tier 1 — expect these only)

- **`main/stage_display_adjust.c`** — split shared **apply** (gap / orientation / invert) from **stage-specific** SPI preview; G4 loop vs G5 loop must call **different** draw functions after apply.
- **`main/panel_hw.c`** / **`main/panel_hw.h`** — new SPI helper(s), e.g. **`panel_hw_draw_g5_alignment_pattern(void)`** (or equivalent name), built from existing **`panel_hw_fill_rgb565`** and small outline primitives; optional **`panel_hw_draw_rect_outline_rgb565(...)`** if not already present.
- **`main/ui_colors.h`** — named constants for **C₀…C₃** (and document hex in SPEC appendix when product locks).

**Tier 2 additionally:** **`main/stage_panel_init.c`** (hook timing, preset list reuse via **`k_spi_chips`** / **`spi_preset_t`** — single source of truth), possibly **`main/session.h`** only if new state is unavoidable (prefer **no** session changes for Tier 1).

### 0.2 Refactor shape (recommended — avoids “one drawable for both stages”)

Keep **one** internal sequence everywhere:

1. **`panel_hw_apply_gap(s)`**
2. **`panel_hw_apply_orientation(s)`**
3. **`panel_hw_apply_invert(s)`**
4. **Stage-specific draw** — **only** this step differs:
   - **G4:** **`panel_hw_draw_top_marker()`**
   - **G5:** **`panel_hw_draw_g5_alignment_pattern()`** (new)
   - **I2C:** unchanged (**`panel_hw_fill_mono`** or documented fallback per §4)

Implement by **replacing** the single **`panel_redraw_after_adjust()`** with either:

- **Option A (preferred):** two small static functions, e.g. **`panel_redraw_for_g4(s)`** and **`panel_redraw_for_g5(s)`**, each calling a shared **`panel_apply_gap_orientation_invert(s)`** (static inline or static helper in the same file), then the correct SPI/I2C draw; **or**
- **Option B:** one function **`panel_redraw_after_adjust(s, preview_kind)`** with an **`enum`** — use **only if** it stays in **`stage_display_adjust.c`** and does not sprawl.

**Do not** add a global “current stage” in **`panel_hw`**; pass intent from **`display_stage_g4` / `display_stage_g5`** only.

### 0.3 Verification after Tier 1 (must pass before merge)

- **`display_stage_g4`** iteration: SPI shows **TOP marker** only (same visual intent as today).
- **`display_stage_g5`** iteration: SPI shows **nested 1 px border pattern** only — **not** TOP marker.
- **I2C:** behaviour unchanged or explicitly degraded per §4 (no silent change to key maps).
- No new **full-frame RGB565 heap buffers** unless justified and measured (§7).

---

## A. Geometry / size confirmation pattern (new — related to G3, not G5)

This is **distinct** from the G5 **1 px nested ruler** (Section 1). It answers: *“Is the session **WxH** actually what I see on glass?”* especially when **active area** is smaller than the chosen logical size (e.g. **128×160** preset vs **128×128** visible window).

### A.1 Visual specification

**Nested rectangles (discrete steps only):**

- Rectangle **sizes** must come from a **fixed list of common geometries** for SPI bench use — the same **family** of presets already used in **manual chip / try-sequence** tables (e.g. **128×128**, **128×160**, **240×240**, **240×320**, **135×240**, **320×480** — exact list = **per-chip union** or **global SPI list**; **product locks the table**). In code, **reuse** the existing **`spi_preset_t` / `k_spi_chips`** data in **`stage_panel_init.c`** (or extract a shared table) — **do not** duplicate ad-hoc arrays in multiple files.
- Draw **from largest to smallest** (or fill order so the **smallest** logical rectangle is **visually on top** / drawn last — z-order locked in SPEC). Each **step** is a **filled rectangle** (or a **1 px frame** at that rectangle’s perimeter — product choice) at **(0,0)** with size **(Wᵢ, Hᵢ)** for preset *i*.
- **No** arbitrary continuous inset steps (e.g. “every 8 px”) — only **preset (W,H)** pairs so the operator maps colours to **named sizes** they can select in the menu.

**Asymmetric top / left refinement (accepted):**

- So the operator can answer **“what colour runs along the **top**?”** vs **“along the **left**?”** without ambiguity, use **different colours** for:
  - **Top strip:** first **t** rows (thickness **scales** with logical size — e.g. **4** or **8** in reference firmware), full width **W**, colour **logical red** (`spi_logical_rgb565[0]`).
  - **Left strip:** first **t** columns, **excluding** the corner already painted by the top strip, colour **logical green** (`spi_logical_rgb565[1]`).
- **Remainder** of the pattern: nested preset rectangles (interior fills or rings) in **other** distinct colours so **edges** of each preset box are identifiable.
- Console prompt example: *“Along the **top** edge (first rows), what colour? Along the **left** edge (first columns), what colour? Which nested size (colour X) matches the visible picture?”*

### A.2 When and fallback

- **When:** After **`panel_hw_spi_init`** succeeds for a **candidate WxH** (manual preset pick or try-sequence trial). **Locked in SPEC §4.2.1:** **after** Phase 0b **OK** and **after** the **orientation-up** probe on **16 bpp** — then size-confirmation pattern, **Enter**, then magenta probe.
- **If no preset matches visually:** Operator chooses **custom WxH** (existing **C** custom flow) and may **re-run** the probe with the new logical size (pattern redrawn for that **W×H**; nested **inner** presets are only those **strictly smaller** than current **W×H** in both dimensions, or **clamp** list — lock in SPEC).
- **ILI9488 / 18 bpp:** Same **intent**; dedicated **`panel_hw`** path when RGB565 stripe fill is not the right primitive (see Section 4).
- **I2C:** **Out of scope** for this nested preset pattern unless SPEC adds a monochrome equivalent (degraded mode).

### A.3 Relationship to G5 (Section 1)

| Pattern | Purpose | Steps / geometry |
|--------|---------|-------------------|
| **Size confirmation (A)** | Confirm **correct WxH preset** vs visible glass | **Discrete** common **(W,H)** only; optional top/left strips **T/L** |
| **G5 alignment (Section 1)** | **Single-pixel** **gap** nudge after size is settled | **Fixed** inset **1 px** rings at **current** session **W×H** |

Do **not** merge the two into one drawable: **different jobs**. Shared primitives (outline helper, fill) are fine.

### A.4 Phase 0b label safe region (cross-reference — partial implementation)

Phase 0b **primary** labels use a **top-left** anchor (`spi_draw_primary_label_top_left` in **`panel_hw.c`**) so **RED/GREEN/BLUE** stay visible when the active window is smaller than logical **H**. **Tier 3** improvements (e.g. stronger scaling for very small panels) may still be tracked here. **SPEC §4.2.1** order remains: Phase 0b **before** size confirmation; wrong **WxH** can still confuse **nested** preset colours until the operator corrects geometry.

---

## 1. Visual specification — G5 nested 1-pixel borders (original plan)

The wording *“solid colour filling the screen with 3 one-pixel borders each one pixel smaller around it”* is parsed as follows (if this matches your intent, lock it in SPEC; if not, see Section 5):

| Layer | Geometry | Role |
|--------|-----------|------|
| **Interior** | Rectangle **inset by 3 px** on all sides from the **active drawable** `(0,0)…(W-1,H-1)` | **Solid fill** — single reference colour **C₀** (e.g. dark grey or navy). This is the “main” solid the user judges for evenness and clipping. |
| **Border 3** | **1-pixel-wide** outline along the inner edge of the inset-2 rectangle (i.e. perimeter of the region that is still 2 px from the physical edge) | Colour **C₃** — high contrast vs **C₀**. |
| **Border 2** | **1-pixel-wide** outline, **1 px further out** than border 3 (perimeter at inset **1** from panel edge) | Colour **C₂** — contrasts with neighbours. |
| **Border 1** | **1-pixel-wide** outline on the **outermost** drawable edge `(0,0)–(W-1,H-1)` | Colour **C₁** — shows **first pixel row/column** vs panel glass; critical for **single-pixel** alignment. |

So you get **three nested rings**, each **exactly 1 logical pixel wide**, stepping **inward** by **1 px** per ring toward the **solid** centre. Together they behave like a **ruler** at the panel edge: misalignment (gap/orientation) shows as **uneven** ring thickness, **clipped** corners, or **missing** outer line.

**Alternative reading (not assumed):** “Each border 1 pixel smaller” could be misread as **stroke width** changing; you specified **1 pixel borders**, so **stroke width is always 1** and **“smaller” means more inset**, as above.

### 1.1 Minimum panel size (degenerate cases)

- For a **non-empty** interior at inset **3**, the inner rectangle needs **(W − 6) ≥ 1** and **(H − 6) ≥ 1**, i.e. **W ≥ 7** and **H ≥ 7** in logical pixels.
- If **W** or **H** is smaller (unusual presets or chips), **either:** (a) **clamp** to a simpler pattern (single outer frame + fill), **(b)** skip inner rings until dimensions allow, or **(c)** show a console warning and fall back to **TOP marker** or solid fill — pick one behaviour and document in SPEC.
- **Odd vs even** dimensions: nested integer rectangles are well-defined; **corner** pixels of 1 px strokes meet at single points — verify on target panels (ST7735/ST7789) for **single-pixel** corners (Section 8).

---

## 2. Behaviour in the guided flow

- **G5 — When:** Each time G5 **redraws** the alignment view (same cadence as today: **before** every key prompt in the G5 loop, after gap / orientation / invert application — see Section 3.1).
- **G5 — Replace or augment:** Replace the current **SPI** preview used during alignment with this pattern **or** use it as a **dedicated “alignment pattern” mode** toggled by a key — **product choice** (Section 5 Q2). Default recommendation: **replace** SPI G5 preview with this pattern for clarity.
- **Size probe (Section A) — When:** Tied to **G3 panel setup** after a **WxH** candidate is applied; **not** on every G5 keypress (unlike the 1 px ruler).
- **Session:** All patterns respect **`gap_col` / `gap_row`** and **orientation / mirror / invert** exactly like **`panel_redraw_after_adjust`** today for the stage where they run; **size probe** may run with **gap zero** only for the **first** size check — **product lock** (recommended: **gap 0** for preset confirmation, then G5 nudges gap).
- **I2C monochrome:** **1 px** features may be **hard to see** or merge at 128×64; see Section 4.

### 2.1 G4 vs G5 independence (implementation intent)

- **G4** continues to use whatever SPI preview is defined for **orientation** (today: **`panel_hw_draw_top_marker()`** via the shared redraw helper).  
- **G5** uses the **nested-border alignment pattern** on SPI (once implemented), **not** the G4 TOP-band preview.  
- **Size probe (A)** is a **third** drawable, used from **G3** (or a dedicated sub-step), **not** G4/G5 unless product explicitly reuses it.  
- **Refactor:** Split today’s **`panel_redraw_after_adjust()`** into (conceptually) **`panel_apply_display_adjustments(s)`** — `panel_hw_apply_gap`, `panel_hw_apply_orientation`, `panel_hw_apply_invert` — followed by **`panel_redraw_g4_preview(s)`** vs **`panel_redraw_g5_preview(s)`** (names illustrative). **Do not** branch on “SPI ⇒ one drawable for both stages.”  
- **Session state** is still shared (orientation and gap both live on **`test_session_t`**); only the **rendered pattern** differs by stage.

---

## 3. Implementation notes (for whoever codes this)

- **SPI RGB565:** Implement as: fill inner rect **C₀**; draw four edges for each of three rectangles (or equivalent scanline fills). Reuse existing **`panel_hw_fill_rgb565`** stripes or add **`panel_hw_draw_rect_outline_rgb565(x, y, w, h, colour)`** to avoid huge full-frame allocs if possible.
- **Size probe (A):** For each preset **(Wᵢ,Hᵢ)** in the **ordered list** (≤ current session size), composite fills and **T/L** strips; avoid O(n) full-frame allocs per layer if possible (draw **largest** background first, then smaller rects). **Preset list** = single source of truth shared with **`stage_panel_init.c`** (or generated from it).
- **Colours:** Pick **C₀…C₃** and **T/L** fixed in **`ui_colors.h`** (or local consts) with **pairwise contrast** (e.g. yellow / cyan / magenta on dark grey interior, or white/black/yellow on grey — **WCAG-like** separation). Document hex values in SPEC appendix.
- **Order of drawing:** Typically **fill interior first**, then draw **outer → inner** or **inner → outer** outlines so corners meet cleanly; test on ST7735/ST7789 for corner **single-pixel** fidelity. **Size probe:** lock z-order so **smallest** preset rect is **topmost** (last drawn).
- **Performance:** Full-screen pattern each keypress is acceptable for bench use; if slow, **dirty-rect** only changed bands (optional optimisation). Many small **`esp_lcd_panel_draw_bitmap`** calls in one redraw can extend **blocking** time — see Section 7 (watchdog / responsiveness).

### 3.1 Codebase cross-reference (`main/stage_display_adjust.c`)

**G5 loop** — `display_stage_g5()` calls **`panel_redraw_for_g5(s)`** at the **start of each iteration** (applies gap/orientation/invert, then **`panel_hw_draw_g5_alignment_pattern()`** on SPI). It prints **`--- STATE ---`** (gap column/row) and **`--- NAV ---`**, then **`serial_read_menu_choice(STAGE_KEYS_G5_GAP, …)`** — keys **`wasd`** (gap), **comma** (revert), **`.`** / **Enter** (save). See current `main/stage_display_adjust.c`.

**Today’s coupling:** **`panel_redraw_after_adjust()`** draws the **same** SPI preview (**`panel_hw_draw_top_marker()`**) for **both** G4 and G5. That matches **neither** the **locked** independence rule (header, Section 2.1) nor the desired G5 nested-border pattern. Implementation **must** call **stage-specific** preview functions after the shared **apply_gap / orientation / invert** trio.

**Keys** — G5 uses **`STAGE_KEYS_G5_GAP`** (`"lruvsx"`) from `display_stages.h`; SPEC **Section 7.3.5** / **Section 9** — **do not** change without SPEC update (Section 10).

**Drawing primitives** — Reuse **`panel_hw_fill_rgb565()`** (DMA stripes, **`esp_lcd_panel_draw_bitmap`** per stripe in `panel_hw.c`) and/or new outline helpers; same **completion** assumptions as **`docs/plan_spi_try_sequence_subtests.md`** Section 12.1.1 (`esp_lcd_panel_draw_bitmap` return, no public flush API).

---

## 4. I2C / ILI9488

| Transport | Note |
|-----------|------|
| **SPI 16 bpp** | Primary target for **G5** pattern and **size probe (A)**. |
| **ILI9488 18 bpp** | Same **logical** layout; implementation must use whatever **`panel_hw`** exposes for **18 bpp** fills/outlines when **`panel_hw_bits_per_pixel() == 18`** — may need a dedicated path if **`panel_hw_fill_rgb565`** is insufficient (verify `panel_hw.c` for ILI9488). |
| **I2C 1 bpp** | **1 px** borders may be **invisible** or **flicker** with dithering. Options: **(a)** 2 px wide “logical” borders, **(b)** checkerboard corner marks instead of rings, **(c)** keep simple fill for I2C only. **Recommend (a) or (c)** until validated on hardware. **Size probe (A):** likely **SPI-only** until a mono variant is specified. |

---

## 5. Questions for product owner (answer to lock SPEC)

1. **G5 geometry:** Confirm **three** **1 px** frames at **insets 0, 1, 2** from the drawable edge, **interior** solid **C₀** from inset **3** — or do you want the **solid to fill the entire screen** with borders **drawn on top** only along the three rings (visually similar; corner math differs slightly)?  
   - **Recommendation:** Lock the **Section 1** table (interior inset **3**) for consistent “ruler” semantics; if full-screen fill with overlays is preferred, spell out **z-order** and corner pixel ownership in SPEC.

2. **G5 mode:** **Always** this pattern on SPI during G5, or **toggle** (e.g. **B** for “borders”) vs a simpler G5 fallback?  
   - **Recommendation:** **Always** nested borders on SPI during **G5** for maximum signal. **G4** stays on **TOP marker** (Section 2.1); optional **toggle** only affects **G5**, not G4.

3. **Colours:** Any **brand** or **accessibility** constraints (colour-blind safe palette)?  
   - **Recommendation:** Four **distinct** hues / luminances; avoid **C₁…C₃** all red/green only — **label legend** in console optional if palette is fixed. **Size probe:** **T** and **L** must differ strongly (e.g. magenta vs cyan) plus distinct **preset ring** colours.

4. **TOP marker:** G4 uses a TOP band; should G5 **also** show a small **TOP** hint on this pattern, or **pure** geometry only?  
   - **Recommendation:** **Pure geometry** in G5 if the nested rings already encode edge reference; **or** a **1-line console** reminder (“outer ring = drawable edge”) without extra on-panel text to avoid covering **1 px** lines. **Independent of G4:** G4’s TOP band is unchanged either way.

5. **Size probe (new):** Exact **ordered list** of **(W,H)** per chip vs one **global** SPI list? **t** for **T/L** strips? Run probe **before** or **after** Phase 0b RGB?  
   - **Recommendation:** **Per-chip** presets **≤** logical **WxH** (from **`spi_presets`** / chip descriptor). **Strip thickness** in reference firmware **scales** with logical size (e.g. **4** or **8** rows/columns), not a single fixed **t=2**. **Locked (SPEC §4.2.1):** run **after** Phase 0b **OK** and **orientation-up** (16 bpp); colours from **`spi_logical_rgb565[]`**; full framebuffer clear first.

**Resolved defaults (until product overrides):** Section 1 geometry; Section 2.1 G4/G5 independence; Section 5 recommendations above; Section 8 test matrix for bring-up; **Section A** coordinate convention (top-left); **T/L** asymmetry; **preset-only** rectangle steps + **manual WxH** fallback.

---

## 6. SPEC impact

- **Section 9 (Alignment):** Describe the **nested 1 px border** reference and that it is for **single-pixel** gap tuning. Align with **Section 9.4** (*blank or brief flash allowed* — a full redraw of a complex pattern qualifies as “brief flash” if implementation clears first).
- **G3 / panel setup:** Add **size confirmation** pattern (**Section A**): **top-left** origin; **T/L** strip colours; **discrete** preset **(W,H)** steps; **manual** entry when no fit.
- **Phase 0b:** Cross-reference **safe label band** (Section A.4) so text stays visible on **128×128-class** windows with **128×160** logical presets.
- **Section 11** (patterns): Cross-reference alignment pattern and **size probe** if listed with test patterns.

---

## 7. Embedded hardening (ESP32 / ESP-IDF)

- **Gap / orientation order:** Same as today — **`panel_hw_apply_gap`** then **`panel_hw_apply_orientation`** then **`panel_hw_apply_invert`**, then **stage-specific** draw (G4 TOP marker vs G5 alignment pattern). Any new **`panel_hw_draw_g5_alignment_pattern()`** (or equivalent) runs **only** on the **G5** path after those three calls. **Size probe** uses the same **apply** order before draw; confirm whether **gap** is forced **0** for the probe (Section A.2).
- **SPI completion:** Each **`esp_lcd_panel_draw_bitmap`** (or stripe) should complete before reusing buffers — same contract as **`panel_hw_fill_rgb565`** (see **`docs/plan_spi_try_sequence_subtests.md`** Section 12.1.1). Outline drawing that reuses one **DMA** buffer for multiple stripes is OK if each **`draw_bitmap`** returns before the next.
- **TWDT / blocking time:** A single G5 redraw may issue **many** bitmap calls (fill + 12 edge segments or more). On large panels at high SPI clocks this is usually fine; on **slow** SPI or **many** segments, confirm the **console task** does not starve watchdog — **yield** between chunks if profiling shows long monolithic runs (e.g. **`taskYIELD()`** or **`vTaskDelay(0)`** between batches on ESP32-C3). **Size probe** may draw **several** full rects — same concern.
- **Memory:** Prefer **stripe-based** fills and **small** line buffers for horizontal/vertical segments over **W×H×2** full-frame RGB565 unless heap headroom is proven.

---

## 8. Test matrix (suggested)

| Case | Expect |
|------|--------|
| **G4 vs G5** (after refactor) | **G4** shows **TOP marker** preview; **G5** shows **nested-border** pattern — never the same SPI drawable for both. |
| **Size probe (A)** | **Only** preset **(W,H)** steps appear; **T** top strip and **L** left strip distinguishable; **manual WxH** path still works. |
| **128×160 / 128×128 SPI** | Operator can identify **wrong** preset from nested colours; Phase 0b **primary** labels use **top-left** anchor (partial safe-band). |
| **gap** at extremes (clipping) | Outer ring **clips** or **vanishes** at edge — operator still sees misalignment; no crash. |
| **ILI9488** (if supported) | Pattern matches **logical** layout; colours correct for **18 bpp** path. |
| **I2C 128×64** | Fallback per Section 4 — readable or documented **degraded** mode. |
| **Very small** logical size (if preset allows **W** or **H** less than 7) | Fallback per Section 1.1 — no negative rects, no heap assert. |

---

## 9. Operational risks and mitigations

| Risk | Mitigation |
|------|------------|
| **Visual noise** — busy nested pattern in G5 | Optional **G5-only** toggle (Section 5 Q2); clear **console** one-liner describing rings. **G4** TOP marker unchanged. |
| **False confidence** on **bad** colour/invert | G5 follows **G4** orientation; pattern does not replace **invert** / rotation checks — SPEC already splits stages. |
| **Slow redraw** — operator taps keys before draw finishes | Unlikely if each keypress **blocks** on `serial_read_menu_choice`; still avoid **queueing** multiple gap steps per incomplete draw (current loop structure is OK). |
| **Size probe confusion** — too many presets on screen | Limit list to **chip-relevant** presets; console **colour legend**; optional **y/n** “does innermost box match?” before full quiz. |

---

## 10. Non-goals

- Changing **wasd** / **comma** / **`.`** / **Enter** key map (**SPEC §7.3.5**, **§9**).  
- Auto-tuning gap (still manual nudge).
- Reordering guided-flow stages (G3 → G4 → G5) or renaming stages.
- Broad refactors of **`panel_hw`** beyond primitives needed for outlines / Tier 2 probe.
- “While we’re here” changes to **`guided_flow.c`**, **`serial_menu`**, or **brand assets** unless strictly required for the tier in scope.

---

## 11. Document history

| Revision | Notes |
|----------|-------|
| Original | Nested 1 px borders, **C₀…C₃**, G5 redraw cadence, I2C/ILI9488 notes, product questions. |
| Prior | **SPEC** section numbers; **codebase** cross-reference (**`panel_redraw_after_adjust`**, **TOP marker** vs mono); **G4/G5** branch note; **minimum size** (Section 1.1); **embedded** Section 7; **test matrix**; **risks**; **defaults** in Section 5. |
| Prior | **Locked:** G4 and G5 **independent** preview paths (Section 2.1); refactor split **apply** vs **G4** vs **G5** draw; Section 5 Q2/Q4 updated; Section 7 / Section 9 aligned. |
| Prior | Renamed scope in title to **G5 + geometry/size confirmation**. Added **Section A** (preset-only nested rectangles, **manual WxH** fallback, **top-left** origin, **T/L** strip refinement). Added **A.4** cross-reference to **Phase 0b safe label band**. Clarified **three** drawables (G4 / G5 / size probe). New Section 5 Q5; test matrix and risks extended; SPEC Section 6 updated. |
| **This update** | **Section 0:** implementation tiers (1/2/3), narrow file list, refactor options, verification checklist; **§7** ESP32-C3 yield note; **§10** expanded non-goals; **§12** agent prompt; preset **SSOT** callout in §A.1. |
| Prior | **SPEC §4.2.1** — G3 size confirmation: after successful SPI init, **before** Phase 0b; **16 bpp** only; nested presets (**smallest on top**); **t=2** top/left strips; **ILI9488** (**18 bpp**) skips. |
| **This update** | **SPEC §4.2.1** order change: **Phase 0b** first, then size confirmation (**workflow**: chipset / RGB–BGR before geometry); plan §A.2 / §A.4 / Q5 aligned. |
| **Doc sync** | Status + §A match **`panel_hw_draw_spi_size_confirmation_pattern`**: mapped colours, scaled strips, inset + double frame, orientation-up before size; §5 Q5 strip note. |

---

## 12. Agent implementation prompt (copy-paste)

Use the block below as the **system or user prompt** for an AI coding agent. The agent should read **`docs/plan_g5_alignment_pattern.md`** from the repo (same workspace as the firmware); **do not** paste the full plan into the chat.

```
You are implementing firmware for an ESP-IDF project (ESP32 class, console-guided LCD bench flow).

AUTHORITATIVE SPEC: Read and follow `docs/plan_g5_alignment_pattern.md` in this repository. That file is the single source of truth for geometry, independence rules, and non-goals.

SCOPE — TIER 1 ONLY unless explicitly asked otherwise:
- Split G4 vs G5 SPI preview so `display_stage_g4` keeps TOP marker and `display_stage_g5` uses a new nested 1 px border alignment pattern on SPI (Section 1, Section 2.1, Section 3, Section 0.2).
- Apply gap → orientation → invert in the same order as today, then stage-specific draw (Section 7).
- Add `panel_hw` support for the G5 pattern using existing stripe fills / small buffers; add named colours in `ui_colors.h` (Section 3).
- Handle W,H < 7 per Section 1.1 with one documented fallback (no crashes, no negative rects).

OUT OF SCOPE FOR TIER 1 — DO NOT IMPLEMENT unless a follow-up message explicitly requests it:
- Section A size-confirmation / nested preset rectangles and G3 hooks.
- Phase 0b safe label band (other doc).
- New keys, keymap changes, or guided-flow stage reordering (Section 10).
- Drive-by refactors outside `stage_display_adjust.c`, `panel_hw.c`, `panel_hw.h`, and `ui_colors.h` for this task.

CONSTRAINTS:
- Preserve I2C behaviour: only change SPI G5 preview; I2C paths stay as today or per Section 4 if you must touch them — do not redesign I2C UX.
- Do not store “current stage” inside `panel_hw`; pass behaviour from `display_stage_g4` / `display_stage_g5` via separate redraw helpers or a local enum in `stage_display_adjust.c` only (Section 0.2).
- No full-frame RGB565 framebuffer allocations unless unavoidable; match existing `panel_hw_fill_rgb565` patterns (Section 7).
- After changes, G4 SPI must still show `panel_hw_draw_top_marker()`; G5 SPI must show only the new alignment pattern (verification Section 0.3 and Section 8).

PROCESS: After editing, briefly list files changed and how manual test on hardware would confirm G4 vs G5 visuals. Do not run `idf.py build` unless the user asks.
```

---

*Document only — firmware changes are implemented per Tier 1/2/3 above. Clarify Section 5 if the visual intent differs from Section 1 or Section A.*
