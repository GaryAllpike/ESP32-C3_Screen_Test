# ESP32-C3 display test — architecture review and implementation guide

**Note:** Sections below that show **FIXED** bugs include **before** snippets for history. Treat those as **resolved** unless the surrounding text says otherwise — do not re-fix from the old code blocks alone.

**Scope:** `main/*.c` and `main/*.h`. Managed ESP-LCD components are out of scope except where integration issues arise at the boundary.

**Methodology:** Full read of every source file, cross-referenced against `SPEC.md`, `board_pins.h`, and the build output. Issues are graded by impact on correctness, safety, and long-term maintainability, not by volume of change required.

---

## 1. What the code does well

Before cataloguing problems it is worth being clear about what is already solid, so fixes are targeted rather than wholesale rewrites.

- **Architecture matches the problem.** A single FreeRTOS task running a linear console flow is exactly right for bench bring-up. There is no accidental RTOS complexity.
- **`session_t` is a clean value type.** Transport fields and display fields are logically grouped. `session_reset_display_fields` preserves transport without requiring the caller to remember which fields to save — that is good API design.
- **`panel_hw` encapsulates ESP-LCD correctly.** The module owns every handle (`s_panel`, `s_io`, `s_i2c_bus`, `s_spi_bus_ok`) and `panel_hw_deinit` tears them all down in dependency order. Error paths call `panel_hw_deinit` before returning, so no partial state leaks.
- **`safe_idle` is genuinely useful.** Driving CS high, BL off, RST high, and SPI lines low before any panel driver touches the bus prevents the accidental ghost-init that burns hours on a scope. The intent is clearly documented.
- **`display_recovery` is a real safety net.** Preserving `bus`, `transport_override`, `i2c_addr_7bit`, and `i2c_ack_seen` across a memcpy-restore is the right call — those are the only fields that can differ between snapshot and current session after a live change.
- **`board_pins.h` comment is exemplary.** Strapping-pin warning, one-commit change rule, and SPEC cross-reference in a twelve-line header is the kind of thing that saves someone three hours at midnight.

---

## 2. Actual bugs

These are not style preferences. They will produce wrong behaviour.

### 2.1 `probe_addr` always returns `ESP_OK`, silently discarding errors

**FIXED:** 2026-04-05 — `identity.c`: `probe_addr` returns bus faults; fast and full scans abort with `printf` + `ESP_LOGE` and delete the bus.

```c
/* identity.c */
static esp_err_t probe_addr(i2c_master_bus_handle_t bus, uint8_t addr_7bit, bool *ack)
{
    esp_err_t e = i2c_master_probe(bus, addr_7bit, 80);
    *ack = (e == ESP_OK);
    return ESP_OK;     /* ← always OK, even if the bus itself faulted */
}
```

If `i2c_new_master_bus` succeeds but the bus subsequently enters a fault state (SDA held low by a broken module, clock-stretching timeout from a slow device), `i2c_master_probe` returns something other than `ESP_OK` or `ESP_ERR_NOT_FOUND`. The function maps everything except `ESP_OK` to `*ack = false`, so a faulty bus and an absent device look identical. The full scan then runs 104 probes against a broken bus, takes several seconds, and reports "no I2C display — using SPI" incorrectly.

**Fix:** Distinguish `ESP_ERR_NOT_FOUND` (address genuinely absent) from everything else (bus fault). Return the underlying error so the caller can abort the scan and report a meaningful message.

```c
static esp_err_t probe_addr(i2c_master_bus_handle_t bus, uint8_t addr_7bit, bool *ack)
{
    esp_err_t e = i2c_master_probe(bus, addr_7bit, 80);
    if (e == ESP_OK) {
        *ack = true;
        return ESP_OK;
    }
    *ack = false;
    /* ESP_ERR_NOT_FOUND is normal (address absent). Anything else is a bus fault. */
    return (e == ESP_ERR_NOT_FOUND) ? ESP_OK : e;
}
```

Callers then check the return value and abort the scan on a non-OK result.

---

### 2.2 `spi_create_panel_for_chip` leaks `s_panel` on `reset`/`init`/`on` failure

**FIXED:** 2026-04-05 — `panel_hw.c`: explicit `reset`/`init`/`on` checks with `fail:` path — `esp_lcd_panel_del(s_panel)` and `s_panel = NULL` before return.

```c
/* panel_hw.c — spi_create_panel_for_chip */
ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(s_panel), TAG, "reset");
ESP_RETURN_ON_ERROR(esp_lcd_panel_init(s_panel), TAG, "init");
ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(s_panel, true), TAG, "on");
```

`ESP_RETURN_ON_ERROR` returns immediately on failure. If `esp_lcd_panel_reset` fails, `s_panel` is non-NULL and is not deleted before returning. The caller (`panel_hw_spi_init`) then calls `panel_hw_deinit`, which will attempt `esp_lcd_panel_del(s_panel)` — that is safe if the panel handle is valid, but the handle was just failed mid-init, which is not always a valid delete target depending on the underlying driver.

**Fix:** Delete `s_panel` before returning on any failure inside this function, then set it to `NULL` so `panel_hw_deinit` does not double-delete.

```c
static esp_err_t spi_create_panel_for_chip(test_session_t *s, session_spi_chip_t chip)
{
    /* ... panel creation switch ... */

    esp_err_t err = esp_lcd_panel_reset(s_panel);
    if (err != ESP_OK) goto fail;
    err = esp_lcd_panel_init(s_panel);
    if (err != ESP_OK) goto fail;
    err = esp_lcd_panel_disp_on_off(s_panel, true);
    if (err != ESP_OK) goto fail;
    return ESP_OK;

fail:
    ESP_LOGE(TAG, "panel post-init failed: %s", esp_err_to_name(err));
    esp_lcd_panel_del(s_panel);
    s_panel = NULL;
    return err;
}
```

---

### 2.3 `display_stage_g8` mutates `i` inside a `for` loop with `i--` on recovery

**FIXED:** 2026-04-05 — `stage_patterns.c` (`display_stage_g8`): `retry_rung:` / `goto retry_rung` (no loop-index mutation). `spi_try_autosequence` uses the same pattern (`retry_trial:`) in `stage_panel_init.c`.

```c
/* display_stages.c */
for (int i = 0; i < n_ladder; i++) {
    ...
    int c = serial_read_menu_choice("yn", s);
    if (c == SERIAL_KEY_DISPLAY_RECOVERED) {
        i--;       /* ← retry same rung after recovery */
        continue;
    }
    ...
    /* rollback path: second read */
    c = serial_read_menu_choice("yn", s);
    if (c == SERIAL_KEY_DISPLAY_RECOVERED) {
        i--;       /* ← same pattern in rollback path */
        continue;
    }
```

This is a hack to re-run the current ladder rung. It works for a single recovery but has two problems:

1. If `@` is pressed repeatedly (or during the rollback confirm), `i` goes negative. The loop condition `i < n_ladder` does not guard against negative `i`, and `ladder[i]` with a negative index is undefined behaviour.
2. The rollback path does a `break` after the second read — if `DISPLAY_RECOVERED` is returned there, the `i--` runs before `break`, corrupting `i` for no benefit since the loop is exiting anyway.

**Fix:** Do not mutate the loop variable. Use a `bool retry` flag or restructure into a `while` loop where retry is explicit.

---

### 2.4 `display_stage_g6` initialises `backlight_pct` inside a stage function

**FIXED:** 2026-04-05 — `stage_display_adjust.c` (`display_stage_g6`): removed `backlight_pct == 0` guard; default remains in `panel_hw_spi_init`.

```c
void display_stage_g6(test_session_t *s)
{
    ...
    if (s->backlight_pct == 0) {
        s->backlight_pct = 75;     /* ← side-effect on session state */
    }
```

This silently changes session state as a side effect of entering a stage. If the user goes P to revisit G6 after G7 or G8, the backlight is set to 75 % and the old value is lost. More importantly, `panel_hw_spi_init` already sets `backlight_pct = 75` when it initialises a fresh panel — so this code exists to paper over the case where `backlight_pct` is 0 after init. The real fix is to ensure `panel_hw_spi_init` always leaves `backlight_pct` at a sensible non-zero default, and remove the fixup from G6 entirely.

---

### 2.5 `display_recovery_restore` overwrites live transport fields, then copies them back — in the wrong order

**FIXED:** 2026-04-05 — `display_recovery.c`: after `memcpy`, only `transport_override` is taken from live session; `bus`, `i2c_addr_7bit`, `i2c_ack_seen` come from snapshot (comment in source).

```c
void display_recovery_restore(test_session_t *s)
{
    session_bus_t bus_keep = s->bus;
    session_transport_override_t tr = s->transport_override;
    uint8_t ia = s->i2c_addr_7bit;
    bool ack = s->i2c_ack_seen;

    panel_hw_deinit();
    memcpy(s, &s_snap, sizeof(*s));   /* wipes everything including bus/addr */
    s->bus = bus_keep;                /* restore live transport */
    s->transport_override = tr;
    s->i2c_addr_7bit = ia;
    s->i2c_ack_seen = ack;

    ...
    /* then re-init using s->bus, s->spi_chip, s->hor_res etc from the SNAPSHOT */
    err = panel_hw_spi_init(s, s->spi_chip, s->hor_res, s->ver_res, pc);
```

The logic is: keep the live transport fields, restore the display configuration from the snapshot. That is the right intent, and for a test tool with a single display this mostly works.

However: the snapshot was taken when `bus` was determined by the probe. The "live" `bus` may differ from the snapshot `bus` after the user has toggled transport override from the expert menu. After the memcpy the snapshot's `bus` is overwritten with the live `bus`, but then `panel_hw_spi_init` is called `if (s->bus == SESSION_BUS_SPI)` — using the live bus, not the snapshot bus. If the user switched to I2C override after taking a SPI snapshot, the restore will try SPI init on an I2C session. This is unlikely in typical use but is a silent correctness hole.

**Fix:** Decide which `bus` controls the restore — it should be the **snapshot** bus (the state you are restoring to), not the live bus. Keep only `transport_override` from live state (since that is a user preference, not a display state). Document this decision explicitly.

---

## 3. Structural problems — these will cause maintenance pain

### 3.1 Three mechanisms for one user action (`!`)

**FIXED:** 2026-04-05 — `display_stage_g3`–`g8` return `bool`; `s_full_restart` / `serial_full_restart_pending` / `serial_consume_full_restart` removed; `!` propagates via `SERIAL_KEY_APP_RESTART` / `SERIAL_WAIT_ENTER_BOOT_RESTART` only.

The "full restart" signal travels through:

1. `static volatile bool s_full_restart` in `serial_menu.c` — a global set by the reader
2. `SERIAL_KEY_APP_RESTART` (-2) returned from `serial_read_menu_choice`
3. `SERIAL_WAIT_ENTER_BOOT_RESTART` returned from `serial_wait_enter_hooks`
4. `serial_full_restart_pending()` polled by stage functions after they return
5. `serial_consume_full_restart()` cleared in `appshell` (and also polled in `display_stages.c`)

The result: adding a new menu requires knowing all five pieces and getting every one right. The intermediate review noted this. The underlying cause is that the global flag was added as an afterthought on top of the return-value mechanism, instead of replacing it.

**The right design:** Use return values only — no global. Every function that calls `serial_read_menu_choice` already gets `SERIAL_KEY_APP_RESTART` back. The problem is that stage functions return `void`, so the restart signal cannot propagate upward. The fix is to give them a return type.

**Implementation:**

1. Change `display_stage_g3` through `display_stage_g8` to return `bool` — `true` = completed normally, `false` = caller should abort (restart was requested). The same convention as `expert_menu` already uses.

2. Remove `serial_full_restart_pending`, `serial_consume_full_restart`, and `s_full_restart`. There is no longer any need for a global.

3. In `guided_flow.c`, the dispatch becomes:

```c
if (k_stage_run[stage] && !k_stage_run[stage](session)) {
    guided_abort_for_restart(session);
    return;
}
```

4. In `serial_wait_enter_hooks`, set the restart flag via a local or return only `SERIAL_WAIT_ENTER_BOOT_RESTART` — the caller checks this and acts; no global needed.

This is one mechanism, one path, readable in a linear trace.

---

### 3.2 Stage dispatch is a manual `if/else if` ladder with duplicated restart checks

**FIXED:** 2026-04-05 — `guided_flow.c`: `k_stage_run[STAGE_COUNT]`, G3–G8 via single dispatch block and one post-call abort path; G3 snapshot remains inline after successful stage run.

**UPDATE (boot / overview):** After `identity_probe_transport`, `guided_show_overview_and_wait()` shows the checklist; **Enter** continues, **E** opens Advanced. Advanced **option 1** returns **directly** into `guided_flow_run` (overview is **not** shown again). The guided loop starts at **`STAGE_G3`** (panel setup); **G2** in code/spec is transport context only — not a separate wait step. Stage titles come from `guided_ui_strings.c` (`guided_stage_meta`); invalid indices map to an explicit **“Unknown step”** row.

`guided_flow_run` currently dispatches stages with:

```c
} else if (stage == STAGE_G3) {
    display_stage_g3(session);
    if (session->panel_ready) { display_recovery_snapshot(session); }
    if (serial_full_restart_pending()) { guided_abort_for_restart(session); return; }
} else if (stage == STAGE_G4) {
    display_stage_g4(session);
    if (serial_full_restart_pending()) { guided_abort_for_restart(session); return; }
...
```

The restart check is copy-pasted six times. G3 has an extra snapshot call that is not present for G4–G8 (snapshot is instead called *inside* G4 and G5 on save, and at the *start* of G7/G8 — inconsistent).

The intermediate review correctly identified the fix: a dispatch table. Here is what it should look like, incorporating the `bool` return convention from §3.1 and resolving snapshot inconsistency:

```c
typedef bool (*stage_run_fn)(test_session_t *s);

/* NULL = stage has no run function (G1 done at boot, G2 info-only, G9 handled inline). */
static const stage_run_fn k_stage_run[STAGE_COUNT] = {
    [STAGE_G1] = NULL,
    [STAGE_G2] = NULL,
    [STAGE_G3] = display_stage_g3,
    [STAGE_G4] = display_stage_g4,
    [STAGE_G5] = display_stage_g5,
    [STAGE_G6] = display_stage_g6,
    [STAGE_G7] = display_stage_g7,
    [STAGE_G8] = display_stage_g8,
    [STAGE_G9] = NULL,
};

/* In guided_flow_run main loop: */
stage_run_fn fn = k_stage_run[stage];
if (fn != NULL) {
    if (!fn(session)) {
        guided_abort_for_restart(session);
        return;
    }
}
```

G2 and G9 remain as inline special cases in the loop; they are genuinely different in nature (no interactive sub-loop, just a print + prompt).

---

### 3.3 `spi_manual_chip_geometry` — deep nesting from chip/preset cross-product

**FIXED:** 2026-04-05 — `stage_panel_init.c`: `spi_preset_t` / `spi_chip_desc_t`, preset arrays, `k_spi_chips[]`; chip banner text preserved via `k_manual_chip_lines[]`; `valid_geo` built on stack; `print_geometry_preset_lines()`.

The function has two nested `for(;;)` loops, a chip-selection dispatch, a geometry-print dispatch (same chips again), a `valid_geo` string dispatch (same chips a third time), and a geometry-resolution dispatch (same chips a fourth time). The same five chips are referenced in four separate `if/else if` trees within one function.

This is the most prominent AI-wandering signature in the codebase: correct, but generated by expansion rather than thought. A human engineer would define the data once and iterate.

**Implementation:**

```c
typedef struct {
    uint16_t    w;
    uint16_t    h;
    const char *label;      /* e.g. "128x128" */
} spi_preset_t;

typedef struct {
    session_spi_chip_t  chip;
    const char         *name;       /* "ST7735" */
    const char         *markings;   /* help text for the menu */
    uint32_t            default_pclk_hz;
    const spi_preset_t *presets;
    size_t              n_presets;
} spi_chip_desc_t;

static const spi_preset_t k_presets_st7735[] = {
    { 128, 128, "128x128" },
    { 128, 160, "128x160" },
};
static const spi_preset_t k_presets_st7789[] = {
    { 240, 240, "240x240" },
    { 240, 320, "240x320" },
    { 135, 240, "135x240" },
};
/* ... etc ... */

static const spi_chip_desc_t k_spi_chips[] = {
    { SESSION_SPI_ST7735,  "ST7735",  "ST7735 / HSGT7735",  20000000, k_presets_st7735,  2 },
    { SESSION_SPI_ST7789,  "ST7789",  "ST7789V / 7789",     20000000, k_presets_st7789,  3 },
    { SESSION_SPI_ILI9341, "ILI9341", "ILI9341 / ILI9342",  20000000, k_presets_ili9341, 2 },
    { SESSION_SPI_ILI9488, "ILI9488", "ILI9488",            10000000, k_presets_ili9488, 1 },
    { SESSION_SPI_GC9A01,  "GC9A01",  "GC9A01 / 9A01 round",20000000, k_presets_gc9a01,  1 },
};
```

The menu-print loop, valid-key construction, and preset resolution all iterate over `k_spi_chips` and its `presets` arrays. Adding a new chip means adding one row and one preset array — no hunting through `if` trees.

---

### 3.4 `display_stages.c` is too large

**FIXED:** 2026-04-05 — `display_stages.c` removed; logic split into `stage_panel_init.c`, `stage_display_adjust.c`, `stage_patterns.c`; `display_stages.h` unchanged as public API.

580 lines is not catastrophic, but the file contains five logically distinct concerns: SPI autosequence, SPI manual selection, I2C init, interactive display adjustment (G4/G5/G6), and automated pattern generation (G7/G8). None of these share internal state; they share only the `panel_hw` API.

**Recommended split (after the dispatch-table refactor from §3.2):**

| File | Contents |
|------|----------|
| `stage_panel_init.c` | `display_stage_g3`, SPI autosequence, SPI manual selection, I2C init helper |
| `stage_display_adjust.c` | `display_stage_g4` (orientation), `display_stage_g5` (gap), `display_stage_g6` (backlight) |
| `stage_patterns.c` | `display_stage_g7` (test patterns), `display_stage_g8` (peak SPI ladder) |
| `stage_util.c` (optional) | `stress_spi_pattern`, `parse_uu`, `panel_redraw_after_adjust` |

Keep `display_stages.h` as the single public header — the internal split is invisible to `guided_flow.c`.

---

### 3.5 `identity.c` and `panel_hw.c` both own an I2C bus config literal

**FIXED:** 2026-04-05 — `display_i2c_bus_config()` in `identity.c`, declared in `identity.h`; used by `identity_probe_transport` and `panel_hw_i2c_init`.

`identity_probe_transport` builds an `i2c_master_bus_config_t` inline. `panel_hw_i2c_init` builds an identical one inline. They are in sync right now; they will drift as soon as someone changes a pull-up or clock source setting in one place and forgets the other.

**Fix:** Define the config once. Options:

1. A `static const` in `board_pins.h` (appropriate since it references `BOARD_DISPLAY_I2C_*` constants).
2. A small `display_i2c_bus_cfg(void)` function in `identity.c` that returns the canonical config, exported for `panel_hw.c` to call.

Option 2 is cleaner because the config struct is an IDF type — keeping it inside a `.c` file avoids polluting the header with ESP-IDF types.

---

## 4. Code quality and readability

### 4.1 G9 handoff dumps on every loop iteration

When `stage == STAGE_G9`, every pass through the main loop calls `handoff_print_session_summary`, printing the full config block, followed immediately by the stage banner from the top of the loop on the *next* iteration. A user who presses `O` explicitly from G9 gets the generator menu (and may print again).

**Fix:** Track stage transitions with a `prev_stage` variable. Only run the G9 entry actions (`handoff_print_session_summary` and "Guided run complete") when `stage != prev_stage` (i.e. on first entry). Update `prev_stage = stage` after the stage-run block.

```c
guided_stage_t stage      = STAGE_G2;
guided_stage_t prev_stage = STAGE_G1;   /* forces banner on first iteration */

/* In the loop: */
if (stage != prev_stage) {
    print_stage_banner(stage, session);
    if (stage == STAGE_G9) {
        handoff_print_session_summary(session);
        printf("Guided run complete. N returns to G2; R restarts from G1; E = expert.\n");
    }
    prev_stage = stage;
}
```

This also eliminates the redundant banner print that currently happens every loop pass when the user is idle at a stage.

---

### 4.2 `serial_read_line` does not honour `!` or `@`

Any menu that takes line input (`i2c_resolve_addr`, custom geometry entry) is a dead end if the display is unreadable. The user cannot see what they are typing; `!` is read as a literal character; the only escape is a chip reset.

This was noted in the intermediate review but the proposed fix ("add a hooked line reader") is the right call and should be implemented. The change is small:

```c
/* Returns number of bytes read, or -1 if ! was pressed (restart requested). */
int serial_read_line_safe(char *buf, size_t cap, test_session_t *session);
```

Internally it mirrors `serial_read_menu_choice`'s handling of `!` (set restart, return -1) and `@` (call restore, continue reading). Rename the current `serial_read_line` to `serial_read_line_raw` and keep it for contexts where no session is available (e.g. before transport identity).

---

### 4.3 Named key-set constants instead of inline strings

**Historical:** The examples below predate **Phase 2 serial UX** (2026). Current keys: **`main/display_stages.h`**, **`KEYS_GUIDED_MAIN`** in `guided_flow.c` (**`.`**/**comma** nav), **`SPEC.md` v1.13**, **`README.md`**.

The valid-key strings in `serial_read_menu_choice` calls are inline literals scattered across five files:

```c
serial_read_menu_choice("npreog", session);     /* guided main: n p r e o g */
serial_read_menu_choice("12345",   session);    /* expert menu */
serial_read_menu_choice("ynq",     session);    /* SPI autosequence */
serial_read_menu_choice("+hviex-", session);    /* G4 orientation */
serial_read_menu_choice("lruvsx",  session);    /* G5 gap */
```

When a key is added or removed from a menu, there is nothing in the compiler's view connecting the string literal to the prompt text above it. Use named constants:

```c
#define KEYS_GUIDED_MAIN   "npreog"   /* n=next p=prev r=restart e=expert o=print g=stage */
#define KEYS_EXPERT_MENU   "12345"
#define KEYS_G4_ORIENT     "+hviex-"
#define KEYS_G5_GAP        "lruvsx"
#define KEYS_G6_BACKLIGHT  "+e-"
#define KEYS_YESNO         "yn"
#define KEYS_YESNOQOPT     "ynq"
```

**Note:** **`M`** is reserved for **SPI manual** in panel setup; the guided shell uses **`E`** for expert. **Phase 2:** guided **Next**/**Previous** are **`.`** and **comma**, not **n**/**p**.

---

### 4.4 Redundant cast and empty `if` in `appshell.c`

```c
if (serial_consume_full_restart()) {
    /* cleared for clean outer loop */
}
```

`serial_consume_full_restart()` is called for its side effect (clearing the flag). The `if` wrapper adds noise without adding information. Either call it as a statement:

```c
(void)serial_consume_full_restart();
```

Or, after the signalling redesign in §3.1, this call disappears entirely.

---

### 4.5 `volatile` on a single-task boolean

`static volatile bool s_full_restart` is single-task; `volatile` buys nothing here except telling the next reader "this might be written from an ISR or another task," which is misleading. Remove `volatile` and add a comment if the design is deliberately single-task.

---

### 4.6 `spi_create_panel_for_chip` sets `pc.bits_per_pixel = 16` twice for ST7735

```c
esp_lcd_panel_dev_config_t pc = {
    ...
    .bits_per_pixel = 16,     /* default for all chips */
};
switch (chip) {
case SESSION_SPI_ST7735:
    pc.bits_per_pixel = 16;   /* ← redundant; already set above */
    err = esp_lcd_new_panel_st7735(s_io, &pc, &s_panel);
```

The ST7735 case was added by copying the ILI9488 case (which legitimately overrides to 18) and not removing the override. Delete the redundant line.

---

### 4.7 LEDC backlight is not silenced on `panel_hw_deinit`

`panel_hw_deinit` frees the SPI bus and panel handles but leaves LEDC running at whatever duty was last set. After `deinit`, the BL GPIO is reconfigured by `safe_idle` (which drives it low), but only at the top of `appshell_run` — not immediately after `deinit`. During the window between `panel_hw_deinit` and the next `safe_idle_configure_display_pins`, the backlight GPIO floats in LEDC output mode.

For a bench tool this is cosmetic. For a product this would be a correctness issue. **Fix:** Add `ledc_set_duty(LEDC_BLK_MODE, LEDC_BLK_CHANNEL, 0); ledc_update_duty(...)` at the end of `panel_hw_deinit` when `s_bl_ledc_ready`. Do not deconfigure the LEDC timer/channel — it is inexpensive to leave configured and reconfiguring on every SPI init cycle would be noisy.

---

### 4.8 RGB565 magic numbers are not named

`0x2945` (neutral grey, used as background fill), `0xF81F` (magenta, autosequence test), `0x18E3` (teal-ish, manual init success), `0xFFE0` (yellow, top-marker bar) appear in `panel_hw.c`, `display_stages.c`, and `display_recovery.c` with no names. The intermediate review noted this. The fix is a `ui_colors.h` header:

```c
#define UI_COLOR_BACKGROUND   0x2945u   /* neutral grey */
#define UI_COLOR_TOP_MARKER   0xFFE0u   /* yellow band */
#define UI_COLOR_PROBE_FILL   0xF81Fu   /* magenta — SPI autosequence */
#define UI_COLOR_INIT_OK      0x18E3u   /* teal — manual init confirm */
```

---

### 4.9 SPEC section citations belong at function/file level, not in `printf` strings

SPEC citations in `printf` messages ("`Init failed — check wiring / driver choice (SPEC §4.3).`") serve two purposes: help the user and help the developer cross-reference. The developer purpose is better served by a comment at the top of the function. The user can be referred to SPEC in a general way without embedding section numbers in runtime output.

Remove inline `SPEC §x.y` from user-facing strings except in `hookup_print.c` and `SPEC.md` itself. Keep them as source comments.

---

## 5. Signs of AI-generated code and their structural fixes

| Pattern in source | Root cause | Human fix |
|---|---|---|
| Same `if (restart) { deinit; return; }` copy-pasted after six stages | global flag instead of return value | §3.1: `bool`-returning stage functions; remove global |
| Four separate chip `if/else if` trees in one function | expansion without abstraction | §3.3: chip descriptor table |
| `valid_geo` string built with same `if/else if` chain as menu print | no data model for the domain | same §3.3 fix |
| `s_full_restart` as `volatile` | copied from ISR-safe pattern without checking context | §4.5: remove `volatile` |
| `pc.bits_per_pixel = 16` set twice | copy-paste of ILI9488 case | §4.6: delete redundant line |
| Empty `if (serial_consume_full_restart()) { }` | code left behind after intent changed | §4.4: remove |
| `int raw = ...; int c = raw;` aliasing in guided_flow | hesitation about variable reuse | merge to one variable |
| Stage banner printed every loop pass | banner not tied to stage transition | §4.1: track `prev_stage` |
| Stale key string vs prompt text | keys added without updating help | §4.3: named constants + §7.3.3 SPEC alignment |

---

## 6. `session_t` design — one forward-looking concern

`profile_tag` is a `char[48]` embedded directly in the struct. When `session_reset_display_fields` is called, the tag is zeroed. When `display_recovery_restore` does `memcpy(s, &s_snap, ...)` and then overwrites transport fields, the tag is correctly restored from the snapshot.

The concern is that `profile_tag` is built by `snprintf` in `panel_hw.c` — a low-level hardware module that arguably should not know about the naming conventions for user-facing strings. If the tag format ever needs changing (e.g. adding a frequency suffix), the change is in `panel_hw.c`, not in the display/UI layer.

This is not an immediate bug. It is worth noting for the next pass: consider having the caller (`display_stages.c`) build the tag after `panel_hw_spi_init` succeeds, and having `panel_hw.c` expose only `panel_hw_spi_chip_name(session_spi_chip_t)` → `const char *`.

---

## 7. Recommended implementation sequence

The items below are ordered to minimise churn — later items build on earlier ones cleanly.

**Step 1 — Fix the actual bugs first (§2)**

These are independent of each other and can be done in any order within the step.

1. `probe_addr`: distinguish `ESP_ERR_NOT_FOUND` from bus faults.
2. `spi_create_panel_for_chip`: delete panel handle on `reset`/`init`/`on` failure.
3. `display_stage_g8`: remove `i--` mutation; use a `retry` flag or restructure loop.
4. `display_stage_g6`: remove `backlight_pct` fixup; ensure `panel_hw_spi_init` sets a sensible default (it already does: line 263 sets `backlight_pct = 75`; the G6 guard is thus unreachable on normal paths and can simply be deleted).
5. `display_recovery_restore`: decide which `bus` drives the restore; document the decision.

**Step 2 — Stage transition tracking (§4.1)**

Add `prev_stage` to `guided_flow_run`. This is the lowest-risk structural change and has immediate UX benefit.

**Step 3 — `bool`-returning stage functions + remove restart global (§3.1)**

Change `display_stage_g3`–`g8` return type. Remove `serial_full_restart_pending` / `serial_consume_full_restart` / `s_full_restart`. Verify `serial_wait_enter_hooks` fits the new contract (it returns its own enum; the caller checks it and propagates).

**Step 4 — Dispatch table in `guided_flow.c` (§3.2)**

After Step 3, the dispatch table reduces to a single post-stage restart check. This is a clean, bounded change.

**Step 5 — Chip descriptor table (§3.3)**

Replace `spi_manual_chip_geometry` with data-driven menu. This is independent of Steps 1–4.

**Step 6 — `serial_read_line_safe` (§4.2)**

Add hooked line reader. Wire into `i2c_resolve_addr` and custom geometry entry.

**Step 7 — Centralise I2C bus config (§3.5) and RGB565 names (§4.8)**

Low-risk cleanup. Do together to keep the diff focused.

**Step 8 — Split `display_stages.c` (§3.4)**

Do last, after all edits within the file are settled. A pure move with no logic changes.

---

## 8. What not to change

- **`session_t` field layout.** It is clean and coherent. Adding fields is fine; restructuring the type would require validating every `memcpy` and `memset` in `session.c`, `display_recovery.c`, and anywhere the struct is stack-allocated.
- **The `panel_hw` / `display_stages` / `guided_flow` layering.** The separation of concerns is correct. Hardware init is in `panel_hw`, stage UI is in `display_stages`, and workflow sequencing is in `guided_flow`. Do not conflate them.
- **`safe_idle`.** It is correct and the comment is good. Leave it alone.
- **`board_pins.h`.** The strapping-pin note and single-commit rule are the right habits. Add to the header, never silently modify.

---

*This document supersedes the previous architecture review. When any fix from §2 or §3 is implemented, mark it with a **`FIXED:`** line under that subsection (date **YYYY-MM-DD** and optional git commit hash when the tree is versioned). §2.1–§2.5 and §3.1–§3.5 were marked **FIXED** on **2026-04-05** (this workspace is not a git repository; no commit hash).*
