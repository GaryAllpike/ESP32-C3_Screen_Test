# Pull request: Phase 2 — Serial UI / UX standardization

**Scope:** `console_text`, `display_stages.h`, `session.c` (truth printout only), `stage_panel_init.c`, `stage_display_adjust.c`, `stage_patterns.c`, `guided_flow.c`, `guided_ui_strings.c`  
**Constraint respected:** No changes to `panel_hw.c` driver logic (text/session labels only elsewhere).

---

## Summary

Standardizes on-screen serial UX: blanking the console at each stage entry, a shared key dictionary (wasd / comma–dot–Enter / orientation toggles), tabular `%-15s :` state lines, and section banners (`--- STATE ---`, `--- CONTROLS ---`, `--- NAV ---`). Guided shell navigation replaces **N/P** with **.** / **comma** to match the global nav map. Internal step codes (**G3**, **G4**, …) are removed from operator-visible strings (C identifiers and comments in non-UI files unchanged).

**Update (Phase 8.x — ergonomic orientation):** **`o` / `x` / `y` / `i`** on orientation-style screens were replaced by **`R` / `W` / `S` / `A` / `D` / `I`** (VanMate layout). **`STAGE_KEYS_G4_ORIENT`** is **`\n.,rwasdi`**. **`R`** collides in name only with guided-shell **Restart** — different menus. See **`SPEC.md` v1.15** / **`README.md`**.

---

## Task 1 — Screen flush

- Added **`console_clear_screen(void)`** in `console_text.c`: prints **`\n` forty times** and flushes stdout.
- Declared in **`console_text.h`**.
- Called at the **entry** of **`display_stage_g3` … `display_stage_g8`** (implementations in `stage_panel_init.c` and `stage_patterns.c` / `stage_display_adjust.c`).

---

## Task 2 — Keymap (`display_stages.h`)

| Role | Keys |
|------|------|
| Directional (gap / row–column nudge) | **w a s d** (W/up row−, S/down row+, A column−, D column+) |
| Stage nav (submenus) | **comma** revert submenu to open values; **.** confirm (often same as **Enter**) |
| Toggles (orientation-style screens) | **R** rotate +90°; **A**/**D** toggle mirror X; **W**/**S** toggle mirror Y; **I** invert |

Removed from stage key sets: **l r u v + - h** (and **, .** as ST7735 **height** nudge — see below).

**ST7735 size screen:** logical memory **width** remains **`[` `]`**; **height** nudge moved from **, .** to **`(` `)`** so **comma** / **period** stay available for revert / confirm.

Macros today:

- `STAGE_KEYS_G4_ORIENT` — `\n.,rwasdi` (G3 Step 2 aliases this macro)
- `STAGE_KEYS_G3_BEFORE_SIZE` — `\n.,wasdxyoi` (legacy superset; **unused** in current C — orientation uses `STAGE_KEYS_G3_ORIENTATION_DISCOVERY`)
- `STAGE_KEYS_G3_SIZE_CHECK` — `\n.,wasd`
- `STAGE_KEYS_G3_SIZE_CHECK_ST7735` — `\n.,wasd[]()`
- `STAGE_KEYS_G5_GAP` — `\n.,wasd`
- `STAGE_KEYS_G6_BL` — `\n.,ws` (**w**/ **s** = brighter / dimmer; **comma** = revert level at open)

---

## Task 3 — Stage renderers & guided shell

### Session truth (`session.c`)

- **`session_print_display_truth`** uses **`--- STATE ---`** and **`printf("%-15s : …")`** for fields; **`where_label`** strings no longer contain **G#** (call sites updated).

### `stage_display_adjust.c` (orientation, alignment, backlight)

- **G4:** **R** rotate, **A/D** mirror X, **W/S** mirror Y, **I** invert, **comma** revert, **Enter** / **.** save; SPI RGB565 shows arrow+UP probe with live **MADCTL** sync; tabular state each loop.
- **G5:** **wasd** gap; **Enter** / **.** save; **comma** revert gap.
- **G6:** **w/s** backlight ±5%; **comma** revert to initial %; **Enter** / **.** done.
- “No panel” paths: short **STATE** / **NAV** hints (**. / Enter** in main menu).

### `stage_patterns.c` (test patterns, SPI speed)

- **G7 / G8:** `console_clear_screen`, **STATE** / **CONTROLS** / **NAV** sections, tabular lines where numeric; labels without **G#**.

### `stage_panel_init.c` (panel setup + SPI subflows)

- **G3:** `console_clear_screen` then **Panel setup** context; I2C and SPI driver menus use **CONTROLS** / **NAV** and tabular lines.
- **Arrow-before-size, size check:** new copy and key handling aligned with wasd / comma / dot; ST7735 **`(` `)`** for height; **comma** on size screen reverts **gap** only to values at entry.
- **`session_print_display_truth`** labels updated (e.g. **Phase 0b**, **Size check**, **SPI manual**, no **G3** prefix).
- Bench tip string: **L/R** → **a/d**; alignment reference no longer cites **G5** in prose.

### `guided_flow.c` / `guided_ui_strings.c`

- **`KEYS_GUIDED_MAIN`:** **`"\n" ".,rago"`** — **comma** = back / revisit wiring from panel setup; **.** or **Enter** = next (and from summary, return to panel setup).
- Switch cases updated; end-of-run text matches.
- **`guided_print_shell_key_help`** and stage **5** `key_extra` updated for **wasd** / **comma** / **.**

### Note: **R** in two contexts

- **Inside** orientation stages: **R** = rotate +90°.
- **At guided shell:** **R** = restart wiring. Same key, different layer; **`O`** = print config (handoff).

---

## Files touched

| File | Change |
|------|--------|
| `main/console_text.c` / `.h` | `console_clear_screen` |
| `main/display_stages.h` | New `STAGE_KEYS_*` |
| `main/session.c` | Tabular display truth, **STATE** header |
| `main/stage_display_adjust.c` | G4–G6 UX + key mapping |
| `main/stage_patterns.c` | G7–G8 UX |
| `main/stage_panel_init.c` | G3 + SPI subflows UX + key mapping |
| `main/guided_flow.c` | **.,** shell keys |
| `main/guided_ui_strings.c` | Help text |

---

## Review checklist

- [ ] **G4:** **o x y i**, **comma** revert, **Enter** / **.** save; panel updates match keys.
- [ ] **G5:** **wasd** moves gap; **comma** revert; **Enter** / **.** save.
- [ ] **G6:** **w/s** brightness; **comma** revert; **Enter** / **.** exit.
- [ ] **Size check (ST7735):** **`[` `]`** width, **`(` `)`** height; **comma** gap revert; **.** / **Enter** continue.
- [ ] **Guided shell:** **comma** / **.** / **Enter** / **R** / **A** / **O** / **G** / **!** / **@** behave as intended.
- [ ] No **G3**–**G8** strings on serial UI (spot-check with monitor).

---

## Verification

Build not run unless you request it. Suggested:

```powershell
. C:\esp\v6.0\esp-idf\export.ps1; cd <project>; idf.py build
```

**Docs:** `README.md` and `SPEC.md` (v1.13) were updated in the same pass as Phase 2 to match this keymap.

*End of PR document.*
