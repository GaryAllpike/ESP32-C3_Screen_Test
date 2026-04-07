# Serial UX overhaul — implementation plan (senior review)

**Audience:** Senior firmware / product engineer  
**Context:** ESP32-C3 display test firmware (`main/appshell.c` → `identity_probe_transport` → `guided_flow_run`, stages in `stage_panel_init.c`, `stage_display_adjust.c`, `stage_patterns.c`; expert submenu in `guided_flow.c`).  
**Intent:** Operator-first console UI: no internal stage jargon (G2/G3) or spec/version strings in normal output; ~60-character line width; clear post-wiring overview; explicit Expert definition; SPI flow = chipset choice then size choice.

---

## 0. Senior review — analysis summary

**What is already solid in the plan**

- Splitting **operator copy** from **internal `guided_stage_t`** is the right move; the enum stays in code and logs.
- Targeting **~60 columns** matches common terminal defaults and keeps text scannable on the bench.
- Treating **Expert** as transport overrides + handoff + resume matches `expert_menu()` today — not a “shortcut to G3”.
- **SPI: chipset then geometry** matches how `display_stage_g3` should behave product-wise; align manual and assisted paths so both land on the same “size” step.

**Gaps to close (improved recommendations)**

1. **Overview + `E` is not a small tweak:** `serial_wait_enter_hooks()` in `serial_menu.c` **discards every key except Enter, `!`, and `@`** until Enter. An “Enter continues / E opens Advanced” screen **cannot** be implemented by reusing that function alone — you need a **new wait primitive** (see §5.3).
2. **Single metadata table:** Replace scattered `stage_name()`, ad-hoc banners in `stage_*.c`, and `serial_print_guided_key_help()` branches with one **stage metadata table** (`title`, `one-line blurb`, optional **per-stage key hints**) to avoid drift and to keep help context-sensitive without repeating “G2/G3” in user strings.
3. **Boot narrative accuracy:** Guided flow **starts at `STAGE_G3`** (panel setup) after the post-identity overview; **G1** is **revisit hookup** (`R` / **`P` from panel setup**). **G2** remains an internal/spec ID for transport context and is **not** a separate wait step in the reference firmware. Overview copy should not imply “step 1 of 9” unless you add a separate operator numbering scheme.
4. **“Expert before identity?”** Today identity runs once after wiring; Expert options 3–5 **already call `identity_probe_transport()`** after changing overrides. Product-wise: keep **identity after hookup**; “force SPI/I2C” remains an **Advanced** action that re-probes — no need to reorder boot unless you want force-transport *before* first probe (unusual).
5. **Global keys:** `!` (full restart) and `@` (display recovery) appear at hookup; repeat them **once** on the overview so operators are not surprised later. Keep behavior identical to `serial_read_menu_choice` / `serial_wait_enter_hooks` handling.
6. **Wrapping:** Prefer a small **word-wrap** helper with a fixed line buffer (bounded stack) over manual `\n` everywhere — but **menus and key legends** may stay **fixed multi-line templates** (wrapping prose is awkward for “Keys: …” lines — split into two 60-col lines by design).
7. **I2C path:** Phase 1 can mirror SPI: plain titles + driver pick in G3; phase 2 only if you add new I2C-specific branching beyond SSD1306 vs SH1106.

---

## 1. Goals

| Goal | Detail |
|------|--------|
| **Operator-first copy** | No “Guided workflow”, SPEC § references, or version strings in normal serial UI. **G1/G2/…** remain **internal** (enums, developer logs); users see plain language (“Wiring”, “Display setup”, “Orientation”, …). |
| **Readable width** | Target **~60 characters** per line for prompts, menus, and body text (wrap helper or manually broken lines). |
| **Clear boot narrative** | After hookup + **Enter** and successful identity, show **one** short “what happens next” screen before the main flow. |
| **Expert discoverability** | On that screen: **what Advanced / Expert is**, **how to open it immediately** (**`E`**), and that it remains available later (true today via main loop). |
| **SPI setup sequence** | **Workflow intro** → **Enter** or **E** → **chipset**: manual list *or* assisted try (y/n profiles) → **size**: presets for that chip *or* custom **WxH**. |
| **I2C path** | Parallel simplified copy (driver pick in panel stage) or explicitly phase-2 — default to **same plain-language pattern as SPI** in one pass if effort allows. |

---

## 2. Non-goals (unless product expands)

- Rewriting **SPEC.md** exhaustively in the same change (add a short note: operator strings vs internal spec IDs).
- Changing **pin map** or **identity probe** logic unless UX requires different ordering (see §6).
- **i18n** in the first pass — but **centralize English strings** in one module or table so translation is a later mechanical step.

---

## 3. Define “Expert mode” (product gap to close)

Today **Expert** is a **numbered submenu**: return to guided, print config / handoff, force SPI, force I2C, clear transport override (`expert_menu()` in `guided_flow.c`). It is **not** “pick chipset faster”; it is **session / transport overrides + handoff reprint + resume** (and options 3–5 **re-run identity** after changing override).

**Required for implementation**

- **User-facing label:** e.g. **“Advanced menu”** or **“Expert tools”** (pick one product-wide).
- **One-line definition:** e.g. *Change how the board chooses SPI vs I2C, reprint your settings summary, then return to the main steps.*
- **Align SPEC/README** with actual menu items; if “jump to any step” is claimed but not implemented, **document the gap** or implement it.

---

## 4. Proposed user-visible flow

### A. Wiring (current concept)

- Hookup text + safe idle + **Enter** (`serial_wait_enter_hooks` in `appshell.c`).

### B. Identity (current)

- `identity_probe_transport` — determines I2C vs SPI for the session.

### C. New: overview screen (insert after identity, before today’s G2 “transport” wait)

- Short bullets (~60 cols): init display → adjust picture → test patterns → optional speed test → **summary to save**.
- Line: **Advanced** — what it does + key **`E`**.
- **Also one line for `!` (full restart) and `@` (restore last good display)** — same semantics as hookup.
- Prompt: **`Enter` = continue** | **`E` = open Advanced now**. **Reference firmware:** Advanced **option 1** returns **straight to panel setup**; the overview is **not** shown again (no redundant second Enter).

### D. Main flow (rename in UI only)

- Replace banners like `--- Guided: G2 ---` with neutral titles (e.g. **Display connection**, **Panel setup**, **Orientation**, **Picture alignment**, …).
- **`N` / `P` / `R` / `O`** (and **`?`** if added): compact help ≤60 cols, **context-sensitive** only — driven from metadata table, not hardcoded “G2/G5” strings.

### E. SPI — panel setup (today’s G3, split in *product* terms)

1. **Controller choice** — Manual: numbered known chipsets (data: `k_spi_chips` / `k_manual_chip_lines` in `stage_panel_init.c`). Assisted try-sequence: **`spi_try_autosequence`** — optional **`W H`** line to filter trials, **ascending area** order (`k_spi_trials`; includes e.g. **ST7735** **130×160** / **132×162** after **128×160**), then per trial **Phase 0b**, **arrow+UP** probe (**ST7735:** **`L`/`R`** column gap), **size** pattern (gap + **ST7735** **`[`/`]`** / **`,`/`.`** memory **WxH** nudge), **magenta** **`y`/`n`/`q`**, and end-of-pass **`c`**/`q` (see **SPEC §4.2**).
2. **Size choice** — After chip: presets from `spi_chip_desc_t.presets` (e.g. **ST7735** lists **128×128** through **132×162**); option for custom **WxH** (existing line input; `!` / `@` remain as today in `serial_read_menu_choice`). Try-sequence **filter** is a separate prompt at the **start of each pass**, not the same as manual custom size.

### F. I2C

- Same plain-language pattern: driver choice without internal stage IDs.

---

## 5. Technical work breakdown

### 5.1 Text layer (~60 columns)

- Add something like `console_print_wrapped(const char *text, unsigned max_col)` with a **bounded buffer** (e.g. line buffer on stack, word-boundary breaks) — avoid unbounded `alloca` / dynamic allocation on ESP32 unless already project style.
- **User-visible** strings only; **`ESP_LOG*`** and file-header comments may keep spec references for developers.
- **Key legends:** often clearer as **two deliberate lines** ≤60 cols than auto-wrapped prose.

### 5.2 String inventory — single source of truth

- Define **`guided_stage_meta_t`** (or similar): `guided_stage_t` → **user title**, **one-line blurb** (for **`G` “where am I”** and optional **`?`**), optional **per-stage key line**.
- Replace ad-hoc **G4/G5/… in stage banners** in `stage_display_adjust.c`, `stage_patterns.c`, `stage_panel_init.c`, `handoff_print.c` with names from the table or shared `#define` user strings.
- Grep cleanup targets (current SPEC/user leakage): `guided_flow.c`, `stage_*.c`, `appshell.c`, `identity.c`, `hookup_print.c`, `handoff_print.c`; optional **`CONFIG_*`** or `#ifdef CONFIG_APP_SERIAL_UX_VERBOSE`** for spec-tagged strings in debug builds.

### 5.3 Overview + Enter/E — **critical API**

**Current behavior:** `serial_wait_enter_hooks()` only returns on **Enter**; **`!`** / **`@`** are handled; **all other keys are ignored** until Enter.

**Required:** a dedicated primitive, for example:

- `typedef enum { OVERVIEW_CONTINUE, OVERVIEW_ADVANCED, OVERVIEW_BOOT_RESTART } overview_wait_result_t;`
- `overview_wait_result_t serial_wait_continue_or_advanced(const char *prompt, test_session_t *s);`

Implementation sketch: read one key in a loop; **`Enter`** → continue; **`e`/`E`** → advanced; **`!`** / **`@`** → same as existing hooks; ignore other keys or **beep** (optional). **Do not** duplicate recovery logic — factor shared `!`/`@` handling with `serial_wait_enter_hooks` / `serial_read_menu_choice` to avoid behavioral drift.

### 5.4 Refactor guided shell copy

- Keep stage enum; replace `print_stage_banner` / `print_transport_line` / `stage_name` user-facing paths with metadata table strings.
- Refactor **`serial_print_guided_key_help(unsigned stage)`** to use **per-stage lines** from the same table (today it hardcodes “G2”, “G5”, long single line — it will violate 60 cols and internal jargon goals).

### 5.5 SPI chipset → size

- Ensure chip selection completes before geometry menu (align manual + assisted paths).
- After assisted match, allow **size-only** change without full autosequence retry — product nicety; track as follow-up if it touches a lot of `stage_panel_init.c` state.

### 5.6 Expert menu

- Rename banner for users; lines ≤60 cols; match README/SPEC; remove **SPEC §** from `printf` paths.

### 5.7 CMake / modules

- If adding `console_text.c` (or `guided_ui_strings.c`), register in `main/CMakeLists.txt`.

### 5.8 Test / acceptance

- Terminal **60×24** (or 80×24): SPI manual, SPI assisted, I2C, **`E` from overview** (requires new wait API), **`!` / `@`** from overview and guided.
- Regression: session state, `display_recovery_snapshot` after panel OK, end handoff, Expert options 3–5 still re-probe identity.

---

## 6. Open decisions for review

1. **Expert before identity?** **Recommendation:** keep **identity after hookup**; Advanced already re-probes when forcing transport. Only revisit boot order if product needs “force SPI before first probe” without entering guided.
2. **Overview every outer loop?** `appshell` re-inits session each full restart — show overview after each successful identity. **Skip intro** on partial flows (e.g. `!` restart) is optional **NVS / session flag** — defer to phase 2 unless needed immediately.
3. **Enter after overview** → straight to **panel setup** vs interim **connection summary:** merge transport (I2C addr / SPI) into **overview** or **first main banner** ≤60 cols; avoid a third screen with duplicate info.
4. **SPEC vs firmware:** Add one paragraph: on-screen labels are **normative for operators**, internal § numbering is for **SPEC ↔ code** traceability — or add appendix mapping **operator name → internal stage**.

---

## 7. Expected file touch list

| Area | Files |
|------|-------|
| Boot / overview | `appshell.c`, `guided_flow.c`, `serial_menu.c` / `.h` |
| Wrapping / strings | new `console_text.c` **or** `guided_ui_strings.c` + extend `serial_menu.c` |
| Main flow copy | `guided_flow.c` |
| SPI chip/size | `stage_panel_init.c`, possibly `display_stages.h` |
| Other stages | `stage_display_adjust.c`, `stage_patterns.c` |
| Identity / hookup / handoff | `identity.c`, `hookup_print.c`, `handoff_print.c` |
| Docs | `README.md`, `SPEC.md` (operator-facing), `docs/architecture_review.md` if it quotes old UX |

---

## 8. Risks and mitigations

| Risk | Mitigation |
|------|------------|
| **Scope creep:** “Jump to any step” Expert | Document as future; keep menu to implemented actions only. |
| **Wrapping:** tables / key legends | Use fixed layouts; wrap only free prose. |
| **Input drift:** new overview wait vs `serial_read_menu_choice` | Share `!`/`@` handling in one internal helper. |
| **String drift:** stages and help text | Single metadata table + grep CI check for `SPEC`/`§` in `printf` (optional). |

---

## 9. Reference — current boot chain (code)

```text
appshell_run:
  hookup_print_instructions → safe_idle → serial_wait_enter_hooks
  → identity_probe_transport → guided_flow_run (starts at STAGE_G2)
```

**Note:** `STAGE_G1` is used for **revisit hookup** (`R` / `P` from G2), not the initial entry.

Expert: `guided_flow.c` — `KEYS_GUIDED_MAIN` includes `e`; `expert_menu()` numbered options 1–5 (3–5 call `identity_probe_transport` after override).

---

## 10. Suggested implementation order

1. **`serial_wait_continue_or_advanced`** (or equivalent) + unit/manual test of keys.
2. **Metadata table** + replace banners / `G` command output / expert title.
3. **Overview screen** wired in `appshell.c` or start of `guided_flow_run()`.
4. **Per-stage help** + `stage_*.c` banner strings; identity/hookup/handoff printf cleanup.
5. **SPI chipset/size** ordering polish if gaps found.
6. **Docs** (SPEC/README) last pass.

---

*Generated from product/UX analysis for implementation planning. Not a commitment to schedule or scope until reviewed.*
