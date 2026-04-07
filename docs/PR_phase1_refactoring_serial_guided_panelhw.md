# Pull request: ESP32-C3 Display Test Suite — Phase 1 refactoring

**Type:** Architectural refactoring (Phase 1)  
**Target branch:** *(as per your workflow)*  
**Author:** *(junior / implementer)*  
**Related directive:** Omnibus Refactoring Directive — Phase 1 (serial line I/O, guided navigation, Phase 0b RGB safety)

---

## Summary

Implements three bounded refactors: blocking VFS-based line input with global hooks, `switch`-based command dispatch in the guided shell, and bitwise-disjoint assertions before Phase 0b secondary colour OR-composition. Session layout, stack allocation of `test_session_t`, and `main/CMakeLists.txt` `REQUIRES` were not modified.

---

## Task 1 — `serial_read_line_safe` (`serial_menu.c` / `serial_menu.h`)

### Problem

Line-oriented prompts used `serial_read_line`, which polled `getchar()` with `vTaskDelay` on `EOF`, bypassing normal blocking console I/O.

### Solution

- Added **`serial_read_line_safe(char *buf, size_t cap, test_session_t *session)`**:
  - Clears **`O_NONBLOCK`** on **`stdin`** once via **`fcntl`**, then reads with **`fgets(buf, cap, stdin)`** (blocking).
  - Strips CR/LF; trims leading spaces only for hook detection.
  - **`!`** alone (after trim) → return **`SERIAL_LINE_BOOT_RESTART` (-1)**.
  - **`@`** alone → **`display_recovery_restore(session)`** if `session != NULL`, print newline, read again.
- **Console note:** Project uses **USB Serial/JTAG** (`CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG` in `sdkconfig.defaults`). **`uart_vfs_dev_port_set_rx_line_endings`** was not added to avoid a new **`esp_driver_uart`** dependency under the locked minimal **`REQUIRES`** list; a comment documents the UART-console analogue.

### Integration

| Location | Change |
|----------|--------|
| `i2c_resolve_addr` | Uses `serial_read_line_safe`; function is now **`bool`**; returns **`false`** on boot restart; invalid hex still reports default **0x3C** and now **sets `s->i2c_addr_7bit`** to match. |
| `spi_try_prompt_resolution` | Takes **`test_session_t *s`**, returns **`bool`**; uses safe reader; **`false`** propagates to abort try-sequence. |
| SPI manual custom geometry (`c`) | Uses safe reader; **`false`** from **`spi_manual_chip_geometry`**. |
| `display_stage_g3` (I2C) | **`if (!i2c_resolve_addr(s)) return false;`** |

### API

```c
#define SERIAL_LINE_BOOT_RESTART (-1)
int serial_read_line_safe(char *buf, size_t cap, test_session_t *session);
```

**Behavioural note:** Unlike `serial_read_line`, the safe path does **not** echo characters from firmware; operators rely on host local echo or blind entry with **`@`** / **`!`**.

`serial_read_line` remains declared and implemented for compatibility; **no remaining call sites** in this tree.

---

## Task 2 — Guided shell navigation (`guided_flow.c`)

### Problem

Post-menu handling in **`guided_flow_run`** used a linear chain of **`if (c == 'x')`** branches.

### Solution

- Replaced with **`switch (c)`** over **`'a'`, `'g'`, `'r'`, `'p'`, `'n'`, `SERIAL_KEY_ENTER`, `'o'`**, with **`default: continue`**.
- **Unchanged by constraint:** Stage table, **`panel_hw`**, **`display_stages`**, return paths, and expert-mode logic outside this dispatch block.

---

## Task 3 — Phase 0b pipeline safety (`panel_hw.c`)

### Problem

Secondary demo builds colours with **`uint16_t` OR** of logical primaries (**`r | g`**, **`g | b`**, **`r | b`**, **`r | g | b`**). Corrupted overlapping masks would silently blend wrong bits.

### Solution

Before the secondary loop in **`panel_hw_spi_run_phase0b_secondaries_demo`**:

- **`assert((r & g) == 0);`**
- **`assert((g & b) == 0);`**
- **`assert((r & b) == 0);`**

Added **`#include <assert.h>`**. Assertions are no-ops in builds that define **`NDEBUG`**.

---

## Constraints honored (do not touch)

| Item | Status |
|------|--------|
| `session.h` / `test_session_t` layout | **Not modified** |
| `test_session_t session` stack allocation in `appshell_run` | **Not modified** |
| `main/CMakeLists.txt` — no new **`REQUIRES`** | **Not modified** |

---

## Files touched

| File | Role |
|------|------|
| `main/serial_menu.c` | `serial_read_line_safe`, blocking stdin setup |
| `main/serial_menu.h` | Declarations and **`SERIAL_LINE_BOOT_RESTART`** |
| `main/stage_panel_init.c` | I2C addr + WxH filter + custom geometry wiring |
| `main/guided_flow.c` | `switch` dispatch in **`guided_flow_run`** |
| `main/panel_hw.c` | Phase 0b secondary assertions |

---

## Suggested review checklist

- [ ] On device: I2C address prompt and geometry / WxH prompts block without busy-delay spin when idle.
- [ ] **`!`** on its own line from those prompts returns to outer restart behaviour as before other **`!`** paths.
- [ ] **`@`** on its own line triggers recovery when session is valid.
- [ ] Guided keys **.** / **Enter**, **comma**, **R**, **O**, **G**, **A** behave per **SPEC §7.3** / **README** (Phase 2 replaced **N**/**P** with **.** / **comma**).
- [ ] Phase 0b secondaries still run on a sane session; intentional corrupt **`spi_logical_rgb565`** in a test build trips asserts if enabled.

---

## Verification

Build was not run in the implementation pass per workspace policy unless explicitly requested. Suggested command (after ESP-IDF export):

```powershell
. C:\esp\v6.0\esp-idf\export.ps1; cd <project>; idf.py build
```

---

## Merge notes

- No database, NVS, or spec version bump required for this PR in isolation.
- If you maintain a changelog or `SPEC.md` revision history, add a one-line entry under the appropriate section when merging.

*End of PR document.*
