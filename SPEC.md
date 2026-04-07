# ESP32-C3 Small Display Test Suite — Product Specification (v1.15)

**Document purpose:** Single source of truth for firmware design and implementation. Suitable for engineering review and handoff to implementers (human or automated).

**Status:** Draft for review.

**Platform:** ESP32-C3, **ESP-IDF** only (no Arduino core). Development workflow: `idf.py build`, `idf.py -p COMx flash monitor` (or equivalent).

**Reference host board:** **Tenstar Robot ESP32-C3 Super Mini** (see §2.1). Firmware hookup text and GPIO assignments **must** use this board’s silkscreen numbering unless the project explicitly targets another module.

---

## 1. Goals

### 1.1 Primary goal

Provide **bench firmware** to exercise **small LCD/OLED modules** after delivery: verify wiring, controller compatibility, geometry (offsets), orientation, color order, and **maximum stable SPI clock** (where applicable), using **human visual confirmation** via the display and **interactive serial** (UART/USB-CDC as configured by ESP-IDF).

### 1.2 Non-goals (v1 — explicit exclusions)

| Excluded | Rationale |
|----------|-----------|
| Touch input | Out of scope |
| Screenshots or frame capture over serial | Out of scope |
| Persistent logging to flash/SD | Keep firmware tight; human records results |
| Non-volatile storage (NVS) of profiles | **Session-only** configuration |
| Encoder, buttons, or auxiliary inputs on carrier boards | Out of scope (e.g. rotary encoder on some breakouts) |
| Automated electrical “chip ID” as sole truth | See §3; **manual profile selection** remains required |

---

## 2. Target hardware

### 2.1 Host — Tenstar Robot ESP32-C3 Super Mini

This specification assumes the development host is the **Tenstar Robot ESP32-C3 Super Mini** module (black PCB). Silkscreen includes **TENSTAR**, **ROBOT**, **ESP32-C3**, **Super Mini**. The same module may be used **alone** (castellated / edge pins) or plugged into the **ESP32-C3 Expansion Board** (see §2.1.1). **GPIO numbers are identical** on module and expansion; hookup instructions for bench use **prefer the expansion board** naming below.

| Feature | Detail |
|---------|--------|
| **USB** | **USB-C** — 5 V power and **USB Serial/JTAG** (or CDC) for `idf.py` flash and `monitor` |
| **Buttons** | **BOOT** (firmware download), **RST** (reset) |
| **Indicators** | Power/status LED (e.g. **PW** near BOOT) |
| **RF** | Onboard ceramic antenna |

**GPIO available on the Super Mini** (use **ESP32-C3 hardware GPIO numbers** in code and in printed hookup tables — **not** abstract “D0…D13” aliases):

Silkscreen layout below matches the **back** of the module with **USB-C at the bottom** (antenna / upper edge at top). **Left column** = antenna side; **right column** = USB side.

| Left column (top → bottom) | Right column (top → bottom) |
|----------------------------|-----------------------------|
| **GPIO 21** | **GND** (marked **G**) |
| **GPIO 20** | **GPIO 0** |
| **GPIO 10** | **GPIO 1** |
| **GPIO 9** | **GPIO 2** |
| **GPIO 8** | **GPIO 3** |
| **GPIO 7** | **GPIO 4** |
| **GPIO 6** | **GPIO 5** |
| **5V** (typically from USB or external 5 V in) | **3V3** (regulated output for peripherals) |

**Usable GPIO set for peripherals:** **0–10**, **20**, **21** (13 GPIOs). **Power:** connect display **GND** to board **GND**; **logic** at **3.3 V** unless the display module specifies 5 V tolerant I/O (some TFT boards level-shift — follow the **display** datasheet).

**Strapping (critical for pin choice):** On ESP32-C3, **GPIO2**, **GPIO8**, and **GPIO9** are **strapping pins** (boot / download behavior). **GPIO0** is also strapping-related on many designs. **Appendix B** assigns **SPI MOSI and RST** to **GPIO20** and **GPIO21** (not **8**/**9**) so display lines do not overlap chip strapping pins. Firmware still assigns **CS, DC, BL** on **GPIO5–7** and **SCK** on **GPIO10**; **I2C** uses **GPIO0/1** — **validate** flash/boot with harness connected. See the [ESP32-C3 datasheet](https://www.espressif.com/sites/default/files/documentation/esp32-c3_datasheet_en.pdf) strapping table.

**Documentation assets:** Reference photos and diagrams live in-repo at **`docs/hardware/C3-Mini/`** (e.g. `C3-Mini_front.jpg`, `C3-Mini_back.jpg`, `C3-Mini_dev_board.jpg`, `C3-Mini_pinout.webp`, `C3-Mini_schematic.webp`, `C3-Mini_pin_use.webp`). Hookup text printed at boot **must** match this board’s pin labels.

**Fixed assignment:** **ESP32-C3** in a **fixed pin assignment** (single bench wiring). GPIO mapping is defined in project configuration / headers — **not** runtime-editable via serial in v1. The **numeric** column in hookup output **must** list **GPIO x** as on the **Super Mini** silkscreen above.

#### 2.1.1 ESP32-C3 Expansion Board (Tenstar Robot) — primary hookup reference

When the Super Mini is mounted on the **ESP32-C3 Expansion Board** (black PCB, silkscreen **ESP32-C3 Expansion Board**; Tenstar family, **GVS** layout matching the module GPIO labels), use this section for **physical** wiring. View as **looking down** at the assembly with the module’s **USB-C toward the right** (antenna / **C3** ceramic toward the **left**).

**GVS rule (each GPIO column):**

| Row (from outside toward the module) | Role |
|--------------------------------------|------|
| **Yellow** | **Signal** — GPIO number printed on the yellow row |
| **Red** | **VCC** — rail **VCC1** (bottom block) or **VCC2** (top block); see power note below |
| **Black** | **GND** |

**Signal pin blocks (yellow row numbers, left → right):**

| Location | Yellow silkscreen (GPIO) | Red rail label | Notes |
|----------|--------------------------|----------------|--------|
| **Top** GVS strip | **21, 20, 10, 9, 8, 7, 6, 5, 4** | **VCC2** | Nine columns; one GVS triplet per GPIO |
| **Bottom-left** GVS strip | **0, 1, 2, 3, 4** | **VCC1** | Five columns |

**GPIO 4** appears on **both** the **top** and **bottom** GVS groups — same electrical net; use either column labeled **4**.

**Dedicated power block (bottom-right):** Three columns of pins (silkscreen along the edge typically **3V3**, **GND**, **5V**) — **red / black / blue** coding on many boards. Use this block for **display supply**:

- **3V3** — logic and panels that run from 3.3 V  
- **GND** — common ground with the display  
- **5V** — only if the **display module** requires 5 V (e.g. onboard regulator); many SPI TFTs still use **3.3 V logic** — follow the **display** silkscreen  

**Recommended hookup pattern for a display:**

1. Connect each **signal** (MOSI, SCK, CS, DC, RST, BL, or I2C SDA/SCL) to the **yellow** pin in the column whose number matches the firmware **GPIO *n***.
2. Connect display **GND** to any **GND** on the expansion board (**black** GVS row or **GND** on the bottom-right power block).
3. Connect display **VCC / 3V3** to the **3V3** pin(s) on the **bottom-right power block** (or the appropriate rail per your display’s label). **Do not** assume **VCC1/VCC2** red rows are 3.3 V without checking your board’s jumper/solder options (some boards select **BAT** / **VCC1** / **VCC2** / **3V3** near the battery connector).

**Other features (informational):** White **JST** for battery, **LTH7R** (or similar) charge IC area — optional for this test firmware; **USB-C** on the module remains used for **flash and serial monitor**.

**Firmware / serial hookup text** should say, for example: *“MOSI → **GPIO 6** → **yellow** pin in column **6** on the **top** GVS strip (expansion board).”* Optionally add: *“GND → black **GND** in that column or bottom-right **GND**.”*

#### 2.1.2 Wire-order–first GPIO assignment (tangle-free jumpers)

**Goal:** Use **Dupont-style** wires with **minimal crossing** from the **display’s 8-pin (typical) header** to the **expansion board** by assigning firmware GPIOs so that **signal order on the screen** matches **physical order along one GVS row** on the host (left → right on the **top** strip, or left → right on the **bottom** strip).

**Typical cheap SPI TFT header order** (pin 1 often at one end of the row; names vary by vendor):

| Order | Silkscreen (common) | Host signal |
|-------|---------------------|-------------|
| 1 | **GND** | Expansion **GND** (black row / power block) |
| 2 | **VCC** / **VDD** | **3V3** (or **5V** only if module requires it) |
| 3 | **SCK** / **SCL** | **SPI SCK** (SPI2) |
| 4 | **SDA** / **SDI** / **MOSI** | **SPI MOSI** |
| 5 | **RST** / **RES** | **RST** (GPIO) |
| 6 | **DC** / **RS** / **A0** | **DC** (GPIO) |
| 7 | **CS** | **CS** (GPIO or SPI CS) |
| 8 | **BLK** / **LED** / **BL** | **Backlight** (GPIO, often PWM) |

**Mapping rule:** After power pins, assign **SCK → MOSI → RST → DC → CS → BL** along the **top** GVS row. **Default manifest (Appendix B)** uses **GPIO 10, 20, 21, 7, 6, 5** so **MOSI** and **RST** are **not** on strapping pins **8** and **9** (physical order on silkscreen **21, 20, 10, 9, 8, 7, 6, 5, 4** — a small gap vs. the old **10…5** block is acceptable for boot safety).

**Priority when choosing the final pin table:**

1. **Boot / strapping safety** (§6.2) — **GPIO8** and **GPIO9** are strapping pins; if placing **CS / DC / RST** on them risks download or reset behavior, **shift** the block to a contiguous run that avoids misuse (e.g. **GPIO 21, 20, 10, 7, 6, 5** — still mostly linear along the top row with one gap — or another set validated on hardware).
2. **SPI2 legality** — **SCK** and **MOSI** must use GPIOs valid for **SPI2** on ESP32-C3 (§5.3).
3. **Wire order** — maximize **straight runs** (display pin order ↔ expansion **yellow** order).

**Serial hookup printout** should list lines in **display header order** (GND → VCC → … → BL), with **GPIO** and **GVS column** so the operator can lay **flat cable** or **parallel DuPont** with few crosses.

### 2.2 Displays under test

- **One physical display at a time** connected to the C3.
- **SPI TFT** modules using common controllers (see §4.1).
- **I2C** path for **small I2C displays** (often monochrome **SSD1306** / **SH1106** class — **exact part may be unknown** at test time; **not** assumed OLED-only — **§3.1**).

### 2.3 Electrical / identification constraints

- Many SPI TFT breakouts are **write-only SPI** (no **MISO** / SDO). **Read Display ID (RDID)** and similar reads are **impossible** on those boards.
- **Panel resolution and row/column offsets** are **not** reliably stored in a queryable form across vendors; **user-selected profile + offset tuning** is the reliable approach.
- **I2C discovery** is more tractable than SPI RDID: **bus scan** yields **7-bit addresses**; **controller type** is **not** implied by address alone — firmware should **probe** (registers / init trials) per **§3.1**.

---

## 3. Identification strategy (realistic)

### 3.1 Boot transport selection (**I2C first**, **SPI** if none)

After **§6.1** (hookup print, safe idle, **Enter**), firmware **must**:

1. **Initialize I2C** on **Appendix B** pins (**SDA/SCL** only — disjoint from SPI).
2. **Discover devices** using a **two-tier** strategy (see **§3.3** for timing):
   - **Fast probe:** ACK check at addresses **most common for I2C displays** (e.g. **0x3C**, **0x3D**, plus any other **project-defined** high-priority addresses).
   - **If** nothing ACKs on the fast list, **optionally** run a **full scan** of valid 7-bit addresses (**0x08**–**0x77**) to catch displays on non-default addresses.
3. **Do not assume “OLED”.** For **each** ACK’d address, run **identification** to the extent possible:
   - **Register reads** where the datasheet defines **device ID** / **revision** (controller-specific).
   - Where **no** ID register exists, **ordered init trials** against a **small table** of **known I2C display drivers** in this project (**§4.4**) — first successful framebuffer / draw wins, else **menu** to pick profile (same as ambiguous SPI).
4. **If** the bus is **empty** or **no** display profile **successfully** matches → **assume SPI**: print one line (e.g. *No I2C display identified — using SPI*), initialize **SPI2** per **Appendix B**, and continue with **SPI TFT** setup (**§4.2**: chipset, then size).

**Ambiguity:** Non-display I2C devices may ACK at common display addresses. **Register probe** + **init trial** reduce false positives; **Force SPI** / **manual profile** remain available (**§7.2**).

**Both buses wired (violation of one-display-at-a-time):** If an I2C display is identified, firmware takes the **I2C** path first; **SPI** lines may still toggle later if code paths mix — operator should **disconnect** the unused harness per **§2.2**.

**Manual override (recommended):** Menu entries **Force SPI** / **Force I2C** (session only); **no NVS**.

### 3.2 Summary table

| Bus | Boot behavior | Authoritative selection |
|-----|----------------|-------------------------|
| **I2C display** | **Auto** scan + **probe** + driver match (**§4.4**) | **Menu** if probe / trials inconclusive |
| **SPI TFT** | **Auto** when **no** I2C display identified | **§4.2** — chipset then **preset or custom size**; **§4.3** **retry** on failed init; optional RDID **if** MISO wired |

**Conclusion:** **Transport** is chosen at **boot** (**I2C** first if a supported display is **found and identified**, else **SPI**).

### 3.2.1 Console logs vs hardware identification

**ESP-IDF vendor panel** init may print lines such as *“LCD panel create success”* and a **version string** (e.g. **`2.0.2`** on **ILI9341**). That string is the **driver / component version** from the Espressif package, **not** a value read back from the panel’s silicon ID. On **write-only SPI** breakouts (**§2.3**), **RDID** is often unavailable anyway — treat **visual** confirmation and **operator-chosen profile** as authoritative.

### 3.3 I2C scan timing (reference — not a hard SLA)

Rough **order-of-magnitude** on ESP32-C3 with ESP-IDF **`i2c_master_probe`**-style traffic (actual numbers depend on **I2C clock** e.g. **100 kHz** vs **400 kHz**, pull-ups, and driver overhead):

| Strategy | Typical work | Approx. duration |
|----------|----------------|------------------|
| **Fast probe** | ACK only at **~2–8** hand-picked addresses | **~1 ms** or less |
| **Full scan** | ACK at **each** valid address **0x08–0x77** (**112** addresses) | **~10–50 ms** (often **tens of ms** at 100 kHz) |

**Ratio:** A **full** scan is on the order of **50–100×** more address probes than a **2-address** fast probe, so **wall-clock** is roughly **proportional** — fast probe stays **sub‑millisecond to a few ms**; full scan stays **well under one human-noticeable second** but is **visibly slower** than fast-only if printed to serial every boot.

**Recommendation:** **Fast probe first**; run **full scan** only if fast finds **nothing** and the product wants **maximum** discoverability.

---

## 4. Supported controller scope (v1)

### 4.1 SPI TFT — target set (~6 profiles)

Implement **approximately six** distinct **init / geometry** profiles in flash, chosen from the most common bench stock. Representative set (exact list is **implementer choice** within flash/RAM budget):

| Family | Typical notes |
|--------|----------------|
| ST7735 | Common glass **128×128** / **128×160**; chip **GRAM** may be wider/taller (e.g. **130×160**, **132×160**, **132×162**) — **no** fixed tab table in firmware; operator picks preset, custom **WxH**, try-sequence trials, or **interactive nudge** on the size-check screen (**§4.2.1**) plus **gap** (**§9**) |
| ST7789 | Common 240×240, 135×240, etc.; **ESP-IDF** `esp_lcd_new_panel_st7789()` (**§5.4**) |
| ILI9341 | e.g. 240×320 |
| ILI9488 | Larger SPI TFTs |
| GC9A01 | Round 240×240 modules |
| Optional sixth | e.g. ILI9163 or another high-occurrence part — **implementer choice** |

**Requirement:** Each profile uses the **ESP-IDF `esp_lcd`** stack per **§5.4** where a vendor driver exists; otherwise **`esp_lcd` panel IO** + **custom** init (**§5.4**).

### 4.2 SPI configuration — two-step menu (chipset, then size)

On the **SPI** path, **do not** combine chipset and resolution in a single flat list of compound profiles unless the UI still reflects the **same** two decisions internally.

**Guided G3 (firmware):** Offer **(M) manual** when the operator can read a **chip marking** on the flex or PCB (then **§4.2** steps below), or **(T) try sequence** when the part is unknown.

**Try sequence (`T`) — reference behaviour:**

- At the **start of each pass**, the operator may enter **`W H`** (e.g. **`128 160`**) to **restrict** trials to that exact resolution, or **Enter** alone to run **all** trials in order.
- Trials are ordered by **ascending pixel area** (**smallest first**): e.g. **ST7735 128×128** → **ST7735 128×160** → **ST7735 130×160** → **ST7735 132×162** → **ST7789 128×160** → **ST7789 135×240** → **ST7789 240×240** → **GC9A01 240×240** → **ILI9341 240×320** → **ILI9488 320×480** (exact table is **implementer-maintained** in firmware).
- After each **successful** `panel_hw_spi_init` for a trial: **Phase 0b** / **color alignment** (see **`docs/plan_spi_try_sequence_subtests.md`**) → **on-panel orientation probe** (arrow + **UP**; **§8** keys — **no** gap nudge on this screen; gap on **§4.2.1** size check or **§9**) → **§4.2.1** size confirmation (**16 bpp** only) → **secondary color test** (magenta-mix fill) stability prompt (**`y`** accept, **`n`** next trial, **`q`** end pass and return to SPI menu).
- When every matching trial in a pass has been considered, firmware offers **`c`** to **cycle** the pass from the first trial (resolution may be re-entered) or **`q`** to quit to the SPI **M/T** menu.

**Manual (`M`)** follows the same **Phase 0b → orientation probe → size check → secondary color test** chain after a successful init (manual path prompt may omit **`q`** — **implementer** choice).

**Phase 0b tri-state:** Primaries **`1`** means **Next** (abandon this chip/size trial), not a “hard fail” label in the UI; **`2`** = colours/labels wrong → format options (**invert**, **manual mapping**, etc.); **`3`** = OK → **secondaries** (yellow / cyan / magenta / white) after a short **look-at-panel** delay, then a second tri-state before leaving 0b.

| Step | Action |
|------|--------|
| **1 — Chipset** | Operator selects **controller family** only (e.g. **ST7735**, **ST7789**, **ILI9341**, **ILI9488**, **GC9A01**, **ILI9163** / custom — per **§4.1** and **§5.5**). |
| **2 — Geometry** | Operator **either** selects from **preset common sizes** for that chip (e.g. 128×160, 240×320 — **implementer-maintained** list per chipset), **or** chooses **custom size** and enters **`hor_res`** and **`ver_res`** (with **validation**: positive integers, **reasonable upper bound** to avoid RAM mistakes). |

**Preset lists** are **per chip**; **custom size** is for odd modules or bring-up. **Offsets** (“tab” / GRAM vs glass) are **not** hard-coded per module: the operator uses **gap** and (for **ST7735**) **memory WxH** tuning on the **size-check** screen and **§9** alignment (**implementer-maintained** keys — **§4.2.1**).

### 4.2.1 G3 SPI — orientation probe, then size confirmation, then secondary color test

After **Phase 0b** completes **OK** on the **SPI** path (**manual** or **try sequence**), reference firmware order is:

1. **Orientation probe (16 bpp):** Small **arrow + “UP”** badge using **session logical primaries** (mapped RGB565 from Phase 0b / manual mapping). The **full chip GRAM maximum** (preset table, e.g. **132×162** class for **ST7735**) is cleared before drawing so prior frames do not leave **ghost** edges. Operator aligns arrow to the **top edge** of visible glass using **§8** (**VanMate** map: **`R`** rotate +90°, **`A`**/**`D`** each **toggle** mirror X, **`W`**/**`S`** each **toggle** mirror Y, **`I`** invert; **comma** reverts; **Enter** or **`.`** done). **Do not** nudge **gap** on this screen — use the **size-check** screen (**`wasd`**) or **§9** after rotation is locked. Firmware refreshes **MADCTL** and repaints the probe on each loop (**`panel_hw_apply_orientation`** + **`panel_hw_draw_orientation_up_probe`**). **Skipped** for **18 bpp** (e.g. ILI9488); use the dedicated Orientation step later.
2. **Size-confirmation pattern** — interactive tuning then **Enter** or **`.`** to continue (see below).
3. **Secondary color test** — solid **magenta-mix** (logical R∨B) fill and stability question (operator UI may say “secondary color test” rather than “magenta probe”).

See **`docs/plan_spi_try_sequence_subtests.md`** for Phase 0b detail and **`docs/plan_g5_alignment_pattern.md`** §A for the size pattern intent. If logical **WxH** is wrong, labels may still be **off-window** until preset/geometry is corrected (**§A.4**).

- **16 bpp (RGB565) only:** The size-check uses the **RGB565** draw path. **ILI9488** at **18 bpp** **skips** this step (same gate as the Phase 0b **RED/GREEN/BLUE** labelled demo).
- **Full framebuffer clear first:** The **entire** driver framebuffer is filled with **logical black** (**R∧G∧B** in RGB565 from the session mapping) so **margins** outside the logical **W×H** pattern are not left as **stale GRAM** / speckle.
- **Nested presets (smallest on top):** Filled rectangles at logical **(0,0)** use **(W×H)** from the **same per-chip preset table** as **§4.2**, only presets **≤** current logical `hor_res`×`ver_res`. Draw order: **largest area first, smallest last**. **Custom WxH** with **no** qualifying preset still draws strips and frame below.
- **Colours from session mapping:** Nested layers and guides use **combinations of** `spi_logical_rgb565[0..2]` (not fixed palette constants) so they stay consistent with **Phase 0b** primaries.
- **Inset guide:** A **1 px** **blue**-semantic outline (**logical B** channel) **inset** from the logical area, **outside** the top/left strip footprint, aids reading **edges** vs strips.
- **Top / left strips:** **Logical top** band uses **logical red**; **logical left** band (below the top band) uses **logical green**. Strip thickness **scales** with panel size (e.g. **4** or **8** rows/columns on larger logical sizes), not a fixed **t=2** only.
- **Logical WxH frame:** A **double 1 px** outline at the **logical** width×height edge uses **R∨G∨B** (bright) so the **session geometry** boundary is easy to see.

**Interactive tuning on the size-check screen (reference firmware, 16 bpp):**

- Console shows **mem WxH** and **gap** values. **`wasd`** nudges **column / row gap** (**`a`/`d`** = column −/+, **`w`/`s`** = row −/+ — **`esp_lcd_panel_set_gap`**, same as **§9**).
- **comma** reverts **gap** to values when the size-check screen was opened (**`.`** / **Enter** do **not** mean height nudge — they **continue** to the next step).
- **ST7735 only:** **`[`** / **`]`** decrease/increase **logical memory width** by **1**; **`(`** / **`)`** decrease/increase **logical memory height** by **1**. Each step **re-inits** the panel at the new **WxH** (same **PCLK**); **session** retains gap, rotation, mirror, invert, and colour mapping. **Clamped** to a small **bench window** (e.g. width **120–144**, height **120–176**); values outside that range use **manual custom WxH** (**§4.2** step 2).
- **`Enter`** or **`.`** exits; if **gap** or **WxH** changed, firmware may **snapshot** display recovery (**implementer** — reference does).
- **`!`** / **`@`** behave like other **`serial_read_menu_choice`** contexts.

### 4.3 SPI chipset selection — retry loop

After **§4.2** completes, firmware runs **panel init**. If **init fails** (`esp_err_t` ≠ OK, timeout, or obvious garbage / no response after a minimal draw), or the operator marks the result as **wrong**, firmware **must not** terminate the session:

- **Default:** **loop back** to **Step 1 (chipset)** in **§4.2** so the user can pick **another** controller or variant.
- **Optional shortcut:** if the operator indicates **wrong size only** (chip OK), loop back to **Step 2 (geometry)** only (**implementer** choice).

**Repeat** until init succeeds and the operator is satisfied, or the user **exits** SPI setup (**implementer** defines key, e.g. **Q**).

**Rationale:** Cheap modules are often mis-labeled; **wrong-chip** is a normal outcome — looping is expected bench behavior (**§14.1**).

### 4.4 I2C displays (not assumed to be OLED)

- **Scope:** Any **I2C** display profile the project supports — **SSD1306**, **SH1106**, **SSD1309**, other monochrome controllers, or **rare I2C TFT** parts if added later. **Do not** assume the panel is OLED from the bus alone.
- **Panel setup (G3, I2C):** Operator chooses **[1] SSD1306** or **[2] SH1106** (default geometry **128×64**). **SH1106** uses **`esp_lcd_panel_set_gap`** with **column gap 2** by default (132-column controller mapped to 128 px glass). **No SPI Phase 0b** or try-sequence on this path. RGB565-style fills used elsewhere are converted to **1 bpp** (non-`0x0000` → lit) before `esp_lcd_panel_draw_bitmap`.
- **Identification:** Prefer **datasheet ID / status registers** where available; otherwise **ordered init trials** and **visual** / **serial** confirmation.
- **Menu (this firmware):** **[1] SSD1306** or **[2] SH1106**, both at **128×64**; **q** skips init.
- **Geometry:** Fixed **128×64** for the two drivers above; extend presets if more modules are added.
- **Boot:** When **§3.1** selects **I2C**, firmware runs the **probe + match** logic in **§3.1** and the test suite for the **resolved** driver; operator may **switch** variant or geometry from the menu if wrong.
- **SSD1306:** Use **ESP-IDF** `esp_lcd` **`esp_lcd_new_panel_ssd1306()`** with **`esp_lcd_new_panel_io_i2c()`** when that controller is selected (**§5.4**).

---

## 5. Software architecture (high level)

### 5.1 Phases

1. **Primary transport:** **ESP-IDF `esp_lcd`** — **`esp_lcd_new_panel_io_spi()`** / **`esp_lcd_new_panel_io_i2c()`** for all SPI/I2C display traffic, plus **vendor panel** factories when Espressif ships them (**§5.4**).
2. **Tests and patterns** use **`esp_lcd_panel_*`** APIs (draw, mirror, gap, etc.) where applicable; **no** parallel “raw SPI only” code path unless required for a **specific** diagnostic (avoid duplication).

### 5.2 Resource stance

- **Consume whole C3** for this tool is acceptable; still avoid **unnecessary** flash (no huge assets, no duplicate full GUI stacks for six chips unless justified).
- **RAM:** line buffers and minimal frame buffers — **implementer choice** per profile.

### 5.3 SPI on ESP32-C3 (Super Mini) — one controller, firmware-chosen pins

- **Single GP-SPI for peripherals:** The ESP32-C3 has **one** general-purpose SPI master block usable for external devices (**SPI2**, sometimes labeled **FSPI** in docs). **SPI0** and **SPI1** are used for **internal flash** (and related) and are **not** the right choice for wiring a display to header GPIOs.
- **Pins are not “hardwired” on the module** to MOSI/SCK/CS: the **Super Mini** exposes GPIOs on headers; **which** GPIO carries MOSI, SCK, optional MISO, and (often) **CS** is chosen in **firmware** when calling **`spi_bus_initialize()`** (and related), within **ESP-IDF** rules for **SPI2** pin mapping on ESP32-C3.
- **GPIO matrix:** Signals can be routed to **many** (not all) GPIOs; **IO_MUX** also defines **default** SPI2 pins in the datasheet / SPI master guide (throughput and timing are best when following Espressif’s recommended SPI2 pins — **implementer** picks a set that matches **§2.1** exposed pins and **§6.2** strapping).
- **Display-specific lines:** **DC** (data/command) and **RST** are **ordinary GPIO**, not SPI peripheral lines. **CS** may be **SPI hardware CS** or a **GPIO** line per driver design; all **TFT SPI traffic** shares **one** SPI2 bus.

### 5.4 ESP-IDF `esp_lcd` — mandatory use when drivers exist

**Policy:** Prefer **ESP-IDF** components over **third-party** or **ad-hoc** bit-bang drivers. Follow the [LCD API](https://docs.espressif.com/projects/esp-idf/en/latest/esp32c3/api-reference/peripherals/lcd.html) (path valid for **ESP-IDF v6.x** / `esp32c3`; adjust if docs move).

| Layer | Use |
|-------|-----|
| **Panel IO** | Always **`esp_lcd_new_panel_io_spi()`** (SPI TFT) or **`esp_lcd_new_panel_io_i2c()`** (I2C displays) for command/data transactions **unless** a documented exception is required. |
| **Vendor panel** | If Espressif provides **`esp_lcd_new_panel_<chip>()`** for that controller in the **installed ESP-IDF**, **must** use it for init and drawing. |

**In-tree vendor panels (ESP-IDF 6.x `esp_lcd` — verify in your `esp-idf` `components/esp_lcd`):**

| Controller | Factory API (typical) | Notes |
|------------|---------------------|--------|
| **SSD1306** | `esp_lcd_new_panel_ssd1306()` | I2C; pair with **`esp_lcd_new_panel_io_i2c()`** |
| **ST7789** | `esp_lcd_new_panel_st7789()` | SPI; pair with **`esp_lcd_new_panel_io_spi()`** |

**Extended chips** (not only in core `esp_lcd`): use **only** the **§5.5** sources — [ESP Component Registry](https://components.espressif.com/) packages vendored under **`managed_components/`** via **`main/idf_component.yml`** + **`dependencies.lock`**, **or** the **documented ST7735** tree under **`components/waveshare__esp_lcd_st7735/`** (not from the Manager in this repo). **Do not** substitute random GitHub or AI-suggested drivers.

**Re-check on ESP-IDF upgrades:** Newer IDF releases may add **`esp_lcd_new_panel_*`** for additional chips — **prefer** upgrading to use them; then **drop** redundant registry dependencies if duplicated.

### 5.5 Canonical LCD driver sources (pinned — no ad-hoc code)

**Rule:** Implementation **must** call into these **known** packages only (plus **core `esp_lcd`**). **CMake `REQUIRES`** names use the **Component Manager** form (e.g. `espressif__esp_lcd_ili9341`) where the driver comes from **`managed_components/`**; **ST7735** uses the **`waveshare__esp_lcd_st7735`** component from **`components/`**.

| SPEC controller (SPI / I2C) | Source | Registry ID (pinned ver.) | License | Notes |
|-----------------------------|--------|---------------------------|---------|--------|
| **SSD1306** | **ESP-IDF** `components/esp_lcd` | *(in IDF)* | Apache-2.0 | `esp_lcd_new_panel_ssd1306()` |
| **ST7789** | **ESP-IDF** `components/esp_lcd` | *(in IDF)* | Apache-2.0 | `esp_lcd_new_panel_st7789()` |
| **ILI9341** | **Espressif** registry | **`espressif/esp_lcd_ili9341` `2.0.2`** | Apache-2.0 | High use; official `esp_lcd` API |
| **GC9A01** | **Espressif** registry | **`espressif/esp_lcd_gc9a01` `2.0.4`** | Apache-2.0 | Round displays |
| **ST7735** | **Waveshare** registry baseline | **`waveshare/esp_lcd_st7735` `1.0.1`** (see note) | MIT | **This repo:** vendored under **`components/waveshare__esp_lcd_st7735/`** (ESP-IDF 6 `rgb_ele_order`); not pulled from Component Manager |
| **ILI9488** | **atanisoft** registry | **`atanisoft/esp_lcd_ili9488` `1.1.0`** | MIT | Widely downloaded; community-maintained |
| **SH1106** | **tny-robotics** registry | **`tny-robotics/sh1106-esp-idf` `1.0.0`** | MIT | **May differ** slightly from pure `esp_lcd_panel_*` — wrap/adapt per upstream README |

**ILI9163** (optional sixth SPI): **No** matching registry package selected — implement with **`esp_lcd_new_panel_io_spi()`** + **project init table** only (same IO layer); **do not** paste unvetted driver code from the web.

**Transitive dependency:** `espressif/cmake_utilities` is pulled automatically for **ILI9341** / **GC9A01** (see **`dependencies.lock`**).

**Project files:** **`main/idf_component.yml`** declares registry dependencies (excludes **ST7735** — local **`components/`** copy); **`dependencies.lock`** pins hashes; **`managed_components/`** holds registry downloads (regenerate with **`idf.py reconfigure`** after manifest edits). **ST7735** sources live in **`components/waveshare__esp_lcd_st7735/`** (`PATCHES.txt` describes IDF 6 API alignment).

**AI / codegen:** When generating firmware, **import headers** from **`managed_components/<namespace>__<name>/`** or the **`components/waveshare__esp_lcd_st7735/`** tree as applicable — **never** invent alternate driver sources.

---

## 6. Boot and safety sequence

### 6.1 Order of operations

1. **Print hookup instructions** to serial: **display pin → ESP32-C3 GPIO** (and power/ground notes). Content is derived from **fixed project pin map**. **Placeholder:** Board-specific silkscreen photos may be referenced in documentation; table in firmware must match **this** project’s wiring.
2. **Safe idle pin state** before aggressive bus activity:
   - **CS** inactive (typically **high**).
   - **SCK / MOSI** idle per chosen SPI mode; avoid clocking garbage into the panel during wiring.
   - **Backlight** **off** if under GPIO control (until deliberate enable).
   - **RESET** released after a defined idle/high behavior per hardware (no uncontrolled glitching).
3. **User acknowledgment** before full init: wait until the operator presses **Enter** (CR and/or LF as accepted by the console) so wiring can finish while lines remain in the **safe idle** state. **Do not** start SPI/I2C clocking or panel init until **Enter** is received.
4. **Bus selection and init** — **§3.1**: **I2C** fast probe (then **full** scan if needed) + **display identification**; **if** a supported **I2C** display is found, run **I2C** tests; **else** **assume SPI**, init **SPI2**, and show **SPI** profile menu.

### 6.2 ESP32-C3 strapping (Tenstar Super Mini / Expansion Board)

- **GPIO2, GPIO8, GPIO9** are strapping pins on ESP32-C3 — see datasheet and §2.1. **Implementer must** choose **CS / DC / RST / BL** so power-on and **BOOT+RST** sequences remain reliable. Document chosen GPIOs in the project pin map and in **§13 print config** output.
- On the **ESP32-C3 Expansion Board** (§2.1.1), connect signals to the **yellow** pins; prefer **3V3** and **GND** from the **bottom-right power block** for the display supply unless the board’s **VCC1/VCC2** jumpers are confirmed for 3.3 V.

### 6.3 Powered target (“hot” connection)

- User requirement: pins must be **as safe as possible** when connecting to an **already-powered** assembly. Interpretation: **idle levels** in §6.1 + **no** long burst of SPI activity until user confirms. Exact electrical guarantees depend on wiring; firmware minimizes **unintentional transactions**.

### 6.4 I2C probe order vs SPI idle

- **I2C** init + **fast/full** scan runs **after** **Enter** so the operator has finished wiring **SDA/SCL** (and **3V3/GND**). **SPI** lines remain in **safe idle** until the firmware commits to the **SPI** path (**§6.1** step 2 still applies before any SPI traffic). See **§3.3** for scan duration.

---

## 7. Serial interface

**Operator copy:** Firmware `printf` text uses plain step titles (no spec section tags). **G1…G9** in this document are **internal stage IDs** for traceability to code — they match the same sequence as the on-screen flow.

**Boot vs §7.3.2:** After identity, the firmware shows a short overview; **Enter** advances directly into **panel setup** (internal stage **G3**). There is no separate wait to “leave G2” with a **Next** key: **G2** remains the spec ID for *transport/identity context*, merged into the panel-setup entry banner in the implementation. If the operator opens **Advanced** from that overview and chooses **Return to main steps** (menu option **1**), firmware **also** advances directly into **panel setup** — the overview is **not** shown a second time.

### 7.1 Transport

- **ESP-IDF console** via **`idf.py -p COMx monitor`** (or equivalent) on the **USB Serial/JTAG** virtual COM port (**same USB cable** as flash). **`sdkconfig.defaults`** sets **`CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y`** so **stdin/stdout** (`printf`, **`getchar()`**) use USB — **required** for interactive prompts when **SPI MOSI/RST** use **GPIO 20/21** (default **UART0** pins; see **Appendix B**). Do **not** switch primary console back to UART0 default without remapping SPI or UART pins.
- **Baud rate:** **115200** is typical for host terminal settings when the transport is USB; UART baud options do not apply to the primary USB console path.

### 7.2 Command philosophy

- **Interactive menus** and single-key commands where practical.
- **Letter keys A–Z** are treated **case-insensitively** in firmware (terminal may send upper or lower case).
- The **same letter** may mean different actions in **different** menus (e.g. **`R`** = **Restart** on the guided shell vs **rotate** in **§8** orientation submenus — **§7.3.5**). Each screen’s **NAV** / **CONTROLS** text is authoritative.
- **Human** marks pass/fail; firmware does **not** upload images.
- **Override:** **Force SPI** / **Force I2C** (session-only) if **§3.1** misclassifies the bus (**optional** but recommended).

### 7.3 Guided workflow (default) vs expert menu (escape)

**Primary UX** is a **guided**, **linear** flow that walks the tester through **identity → configuration → testing → handoff**, with **navigation** at every stage. A separate **expert / free** mode is always reachable.

#### 7.3.1 Guided flow — purpose

- **Identity:** Boot **I2C** probe + classification (**§3.1**); **SPI** path selection (**§4.2**–**§4.3**) when applicable.
- **Configuration:** Orientation / TOP (**§8**), alignment (**§9**), backlight (**§10**, SPI TFT only).
- **Testing:** Standard pattern sequence (**§11**), then peak SPI tuning (**§12**, SPI TFT only).
- **Handoff:** At the **end** of the guided path, firmware **prints the §13 handoff / “print config” block** — **automatically** after the last guided step. From any guided screen, **`O`** (**O**utput config) reprints the same block on demand. **Do not** require the operator to remember the key from cold; the guided path **culminates** in the handoff output.

#### 7.3.2 Guided stages (reference order)

Exact labels are **implementer** choice; **logical** order **must** match:

| # | Stage |
|---|--------|
| G1 | Hookup acknowledged (**Enter**); safe idle (**§6**) |
| G2 | Transport / identity — **I2C** probe result **or** **SPI** assumed (**§3.1**) |
| G3 | **SPI:** **§4.2** — **manual** or **try sequence** (**§4.2**); after **successful** init, **Phase 0b** → **orientation probe** (arrow+UP, **16 bpp**, **§8** keys; **no** gap on orientation screen) → **§4.2.1** size confirmation (gap + **ST7735** **`[`/`]`** / **`(`/`)`** memory **WxH** — **16 bpp**; **ILI9488** / **18 bpp** skips orientation+size probes parallel to Phase 0b RGB demo); secondary color test / link prompt; init + retry (**§4.3**). **I2C:** driver + geometry if not auto-resolved (**§4.4**). |
| G4 | Orientation / TOP / advanced mirror (**§8**) |
| G5 | Alignment (**§9**) |
| G6 | Backlight (**§10**, SPI TFT only; **skip** or one-line **N/A** for I2C) |
| G7 | Standard test sequence (**§11.1**) — may be **one** “run all” step or **sub-steps** per pattern |
| G8 | Peak SPI (**§12**, SPI TFT only; **skip** for I2C) |
| G9 | **Handoff — §13 print config** (**`O`**) — **end of guided run** |

**Restart** returns to **G1** (or **G2** after hookup — **implementer** choice; document which).

#### 7.3.3 Navigation (every guided screen)

At each guided stage, offer:

| Action | Meaning |
|--------|---------|
| **Next** | Advance to the **next** stage (validate current stage if required, e.g. init OK). |
| **Previous** | Go back **one** stage **without** losing session state unless that stage **re-runs** init (e.g. changing chipset may require re-init — **implementer** defines). |
| **Restart** | Begin guided flow again from **G1** / **G2** (clear or preserve session per **implementer** — default: **session RAM** only, **no NVS**). |

**Key mapping** is **implementer** choice; document in README and serial help text. **Reference firmware** uses single letters on the **guided shell** (the prompt after each stage): **`.`** or **Enter** Next, **comma** Previous, **`R`** Restart (typically **G1** hookup revisit), **`E`** **Expert** menu, **`O`** print config / **§13** handoff, **`G`** optional **stage name** reminder. **`!`** / **`@`** may denote full restart and display recovery (**implementer**-defined).

**G2 vs G3:** On **G2** (transport / identity summary), **`M`** and **`T`** must **not** be advertised as active keys unless they are handled there — they belong to **G3 SPI** panel init (**§4.2**: **M** = manual marking, **T** = try sequence). Operator should advance into **G3** (**.** / **Enter**) before pressing **`M`** / **`T`**.

The same letter may mean different things in **different menus** (e.g. **`D`** in one context vs another) as long as each screen’s prompt matches its accepted keys.

#### 7.3.4 Escape to expert menu

- At any time, a dedicated action (e.g. **`E`** **Expert**) **exits** the guided **shell** and shows an **expert menu**: **direct access** to major steps — orientation, alignment, backlight, **individual** tests (**§11.2**), peak SPI, **print config** (**§13**, same as guided **`O`**, also a **numbered** expert entry), **Force SPI** / **Force I2C**, etc. (**Do not** overload **`M`** for Expert on the guided shell if **`M`** = **manual** in **G3 SPI**.)
- From expert mode, an option **Return to guided** resumes at a **defined** stage (e.g. current **G#** or **G1**).

#### 7.3.5 Keys — **print config** vs **guided nav** vs **stage submenus**

- **§13** “print config” / handoff uses **`O`** (mnemonic: **O**utput). **Guided shell** **Previous** uses **comma** — **do not** overload **`O`** for “previous.”
- **Orientation** submenus (**§8**, arrow-before-size, G4): **`R`** = rotate +90°; **`A`** / **`D`** = each toggles mirror X; **`W`** / **`S`** = each toggles mirror Y; **`I`** = invert; **comma** = revert submenu; **Enter** or **`.`** = confirm. **On the guided shell**, **`R`** is **not** rotate — it is **Restart** (same glyph, **different menu**; README and on-device help document both). **`O`** remains **print config** on the guided shell.
- **§9** alignment uses **`wasd`** for gap nudge (**`s`** = row down). **`W`**/**`S`** on orientation screens (**§8**) toggle mirror Y — not row gap (**§9**).
- Firmware avoids **`D`** in menus to reduce confusion with historical “dump” wording.

---

## 8. Orientation and “default top”

### 8.1 Logical vs physical

- After controller init, memory scan order defines a **logical** “top” of the framebuffer.
- Physical mounting may differ. Provide:
  - A **visible pattern** with a clear **“TOP”** label or arrow on the **logical** top edge.
  - **Rotation** control: **0° / 90° / 180° / 270°** (or equivalent **MADCTL** presets per chip).

### 8.2 Mirror (flip horizontal / vertical)

- **Include** optional **flip H** / **flip V** (mirror) toggles under an **“Advanced orientation”** (or similar) submenu.
- **Rationale:** Less common than pure rotation, but **required** when rotation alone cannot correct handedness / scan vs glass bonding.

### 8.3 Handoff

- Final **MADCTL** (or equivalent), **swap/mirror** flags, and **offsets** must be representable in **§13 print config** output.

### 8.4 Reference firmware — orientation keys (16 bpp SPI)

Single-key menus (**`serial_read_menu_choice`**): **`R`** rotate +90°, **`A`** / **`D`** each **toggle** mirror X, **`W`** / **`S`** each **toggle** mirror Y, **`I`** invert display, **comma** revert values for the current orientation submenu, **Enter** or **`.`** save and exit the submenu. **Gap** is **not** adjusted on this submenu — **§4.2.1** size check and **§9** use **`wasd`** for column/row gap. G4 **SPI RGB565** uses the same map; **I2C** / **18 bpp SPI** still show a **TOP** bar with the same **letter** keys updating session + redraw.

---

## 9. Alignment (offsets)

### 9.1 Purpose

Tune **column offset**, **row offset**, and **active width/height** (as applicable) so test patterns **fill** the visible glass. **Reference firmware:** **column/row gap** and (**ST7735**) **memory WxH** may already be adjusted during **§4.2.1**; **§9** remains the dedicated alignment step with the **G5** pattern.

### 9.2 Controls

| Key | Action |
|-----|--------|
| **a** | Nudge **left** (column gap −1) |
| **d** | Nudge **right** (column gap +1) |
| **w** | Nudge **up** (row gap −1) |
| **s** | Nudge **down** (row gap +1) |
| **Enter** or **.** | **Save** alignment to **session RAM**; **return** to parent menu |
| **Comma** | **Revert** gap to values when this alignment screen was opened |

### 9.3 Step size

- **1 pixel** per nudge **initially** (configurable later if needed).

### 9.4 Blank during change

- **Allowed:** full-screen **blank** or brief flash while applying offsets if implementation simplifies redraw.

### 9.5 Scope of **comma** (revert)

- **Alignment screen only:** gap values when the operator opened the picture-alignment step. **Does not** revert backlight level or **peak SPI MHz** unless explicitly specified elsewhere.

---

## 10. Backlight (PWM)

### 10.1 SPI TFT

- **Range:** **0–100%** in steps of **5** (i.e. 0, 5, 10, …, 100).
- **Interaction:** User adjusts until **comfortable for viewing** (subjective). **Reference firmware:** **`w`** / **`s`** (brighter / dimmer in 5% steps), **comma** reverts to level when the submenu opened, **Enter** or **`.`** done — **do not** reuse alignment **wasd** in the same prompt.

### 10.2 I2C display

- **Dimming N/A** for typical small I2C modules (no separate BL pin). Menu should **omit** or **skip** with a one-line explanation.

---

## 11. Test patterns and sequences

### 11.1 Standard sequence (fixed order)

Every **full automatic run** starts from the **same** ordered list (implementer fills exact order):

1. **Solid fills:** black → white → primary RGB (detect **RGB vs BGR** / wrong color order).
2. **Gradients:** horizontal and vertical (banding, stuck bits).
3. **Grid / fine checkerboard** (wrong resolution, SPI glitches, aliasing).
4. **Border / corner markers** (offsets, visible area).
5. **Display inversion** (if supported by controller — verify command path).
6. **Text in corners** (readability, residual offset).

### 11.2 Individual tests

- Menu shall allow **running any single test** without running the full sequence.

### 11.3 Stress (“pressure”) before trusting SPI speed

- At each candidate SPI frequency (§12), run a **time-boxed** stress pattern (animated / repeated updates — **not** a single frame).
- **Default stress duration:** **implementer choice** (e.g. **5 seconds** per step — document chosen default in code/README).

---

## 12. Peak SPI clock discovery

### 12.1 Goal

Find **highest stable SPI clock** for the **current wiring and panel**, with **human** judging stability.

### 12.2 Ladder

- Use **coarse, common** frequency steps only — **not** 1 MHz increments. Example shape (exact values **implementer choice**, board-dependent): **10 → 20 → 26 → 40 → 53 → 80** MHz (or subset). Upper bound should avoid absurd values on long jumper wires.

### 12.3 Procedure

1. Start at a **low** safe frequency; run **stress** (§11.3).
2. Prompt operator: **`y`** = OK, **`n`** = fail.
3. **Increase** to next ladder step; repeat stress + prompt.
4. On **first failure** at a new step: **rollback one ladder step** from the failing candidate; run stress again; operator confirms **OK**.
5. **Store** the **confirmed maximum stable frequency** for inclusion in **§13** output.

### 12.4 Relationship to **§13**

- Peak MHz is **session data**; included in **§13 print config** output, including at **guided** completion (**§7.3**).

---

## 13. Print config / handoff — **`O` command**

### 13.1 Trigger

- **Guided flow:** Handoff is printed **at the end** of the guided run (**§7.3.1**), **without** relying on the operator to know **`O`** in advance.
- **Guided (any stage):** User presses **`O`** to **print** the same block **on demand** (when a profile exists; GPIO map always applies).
- **Expert menu:** A **numbered** item prints the same block (equivalent to **`O`**).
- Saving alignment (**Enter** / **`.`** in **§9**) **does not** auto-print handoff.
- **§7.3.5:** **comma** is **Previous** on the guided shell; **`O`** is print config; alignment row-down is **`S`** (**§9**).

### 13.2 Content (conceptual)

Print **machine- and human-readable** summary sufficient to recreate init in a **library** (esp_lcd, LovyanGFX-style, etc.). **No obligation** to emit C source code — **variables and values** only.

Include as applicable ( **omit** keys that do not apply to the active bus/profile — **no** long lists of “N/A”):

- **Profile identifier** (chip family + geometry preset name).
- **GPIO map:** MOSI, SCK, CS, DC, RST, BL (SPI); **SDA, SCL**, **7-bit address** (I2C).
- **Geometry:** `hor_res`, `ver_res`.
- **Offsets:** `col_offset`, `row_offset` (or project’s named equivalents).
- **Orientation:** rotation, **flip H/V**, **MADCTL** value or decomposed flags, **RGB/BGR** if relevant.
- **Invert display** flag if used.
- **Backlight** percent (SPI TFT).
- **Peak SPI clock (Hz)** from §12 (SPI TFT only).
- **Ordered init data:** register writes as **(command byte, [data bytes…])** sequences or equivalent structured list.

### 13.3 I2C display profile

- When profile is **I2C** (not SPI), **§13** output contains **only** I2C-relevant and geometry/driver fields.

---

## 14. Failure handling

### 14.1 Init / communication failure

Distinguish **recoverable** vs **fatal**:

| Case | Behavior |
|------|----------|
| **SPI: wrong chipset / geometry** (init error, snow, obvious wrong controller) | **Do not** hard-abort the app. **Loop back** per **§4.3** (typically **§4.2** Step 1 — chipset). Optionally print one line: *Try another profile.* |
| **SPI / I2C: suspected wiring / power** (no ACK, bus error, repeated failure **across several** profiles) | **Abort** current bring-up; print **§14.2** troubleshooting tips. **No infinite loop** on the same failing hardware — operator fixes wiring and **resets** or **re-runs** from boot. |
| **I2C: driver ambiguous** | Return to **I2C** driver / geometry flow (**§4.4**, mirror **§4.2** where practical). |

### 14.2 Static troubleshooting tips (on fatal or user-requested help)

Print **short** bullets, e.g.:

- Check **GND** common.
- Verify **3.3 V logic** (and regulator on module).
- **CS / DC** not swapped; **MOSI vs SCK** correct.
- **SPI clock too high** — lower and retry (after a profile works at low speed).
- **Wrong profile** — use **§4.2** / **§4.3** to pick another chipset or size.

**Implementer** expands as needed; keep **brief**.

---

## 15. Operator workflow (summary)

**Default:** follow the **guided** flow (**§7.3**): identity → configuration → tests → **§13 print config** at the end. Use **.** / **Enter** (Next), **comma** (Previous), **`R`** (Restart); use **`E`** (or your documented key) for **expert menu** when needed.

1. Flash firmware; open monitor; read **hookup**; wire **either** the **I2C display** (**Appendix B** bottom) **or** the **SPI TFT** (**Appendix B** top); maintain **safe idle**; **Enter** when ready.
2. **Boot** runs **I2C** fast probe (then **full** scan if needed) + **identify display** — **if** a supported **I2C** display is found, tests use **I2C**; **if not**, firmware uses **SPI**: **§4.2** (**chipset** → **preset or custom size**), then init. **If SPI init fails**, **loop back** (**§4.3**).
3. **Guided:** orientation / TOP (**§8**); alignment (**§9**); **Enter** / **`.`** / **comma** as in §9; **§7.3.5** for **`O`** / **comma** / **`wasd`**.
4. **Guided:** backlight (**TFT** only, **§10**).
5. **Guided:** standard test sequence (**§11.1**); optional **individual** tests via **expert** menu (**§11.2**).
6. **Guided:** peak SPI (**SPI TFT** only, **§12**); **`y`/`n`** per ladder step.
7. **Guided completion:** print **§13** — end of guided run (and use **`O`** anytime to reprint).
8. **Expert menu:** print config (**§13**) on demand; jump to any step (**§7.3.4**).
9. Power cycle loses session (no NVS).

---

## 16. Pin map and documentation inputs

### 16.1 Fixed wiring

- **All GPIO assignments** are **compile-time** constants for v1 — **no** runtime pin selection, **no** menu to reassign pins.
- **Host identity:** **Tenstar Robot ESP32-C3 Super Mini** — GPIO reference table in **§2.1**.
- **Wire order:** Default GPIO map **must** follow **§2.1.2** (tangle-free alignment with typical display header order) unless a documented **strapping** or **SPI2** constraint forces a small deviation.
- **Single source of truth:** The **exact** GPIO number for each **canonical signal** lives in **one** firmware header (or equivalent), e.g. `main/board_pins.h` — **Appendix B** must **match** that file **bit-for-bit** on every release.

### 16.2 In-repo reference material

- **`docs/hardware/C3-Mini/`** — photographs and vendor diagrams for the **Super Mini** and optional **GVS expansion** (`C3-Mini_front.jpg`, `C3-Mini_back.jpg`, `C3-Mini_dev_board.jpg`, plus `.webp` pinout/schematic/pin-use). Implementers and **serial hookup** text **must** stay consistent with these labels.
- If the board revision changes silkscreen, update **§2.1** and the assets folder together.

### 16.3 Hookup output format (firmware)

- Printed instructions **must** name:
  - **Board stack:** e.g. `Tenstar Robot ESP32-C3 Super Mini` on **`ESP32-C3 Expansion Board`**
  - Each signal: **function** → **`GPIO <n>`** → **expansion board location** per §2.1.1 (e.g. “**top** GVS, yellow column **7**” or “**bottom** GVS, yellow column **2**”)
  - **Power:** **GND** + **3V3** (or **5V** if required) using **bottom-right power block** where possible
- **Do not** rely on Arduino-only pin aliases unless a translation table is included.
- **Vendor silkscreen vs canonical names:** Printed hookup and **§13** output use **correct electrical names** (**SCK**, **MOSI**, **RST**, **DC**, **CS**, **BL**). If the display says **SCL** / **SDA** / **RES** / **LED**, map them mentally to **SCK** / **MOSI** / **RST** / **BL** — the **GPIO** column is always the same for that **role**.

### 16.4 Canonical pin manifest (hard-coded)

**Requirement:**

1. **One manifest** — Every signal is assigned a **fixed** `int` GPIO (or `-1` / `GPIO_NUM_NC` where inapplicable) in **exactly one** place in the source tree (e.g. `#define PIN_DISPLAY_SCK 10` or `enum` + `static const` table).
2. **Immutable across sessions** — Same firmware binary ⇒ same pins forever; operators can **memorize** or **label** a harness.
3. **Appendix B** — The **authoritative** table for humans; **must** equal the header after any change (update **both** in one commit).
4. **Display side** — User wires **display pin 1…8** to the **same** host GPIOs **every** time per **Appendix B**; misleading vendor labels are ignored in favor of **role** (see §16.3 bullet on canonical names).
5. **SPI / I2C exclusivity** — **I2C** SDA/SCL must **not** use any GPIO assigned to **SPI** (see **Appendix B**). **SPI** and **I2C** are never active on the same harness; **disjoint** pins avoid shared-pin mistakes and keep **wiring** physically separable (top GVS strip vs bottom).

**Build-time checks (recommended):** Assert no duplicate GPIO; assert **SPI** and **I2C** sets are **disjoint**; assert SPI2 + IOMUX rules satisfied; optional static assert strapping review.

---

## 17. Implementation notes (non-binding)

- **`esp_lcd`** is the **default** stack for all display I/O (**§5.4**); vendor **`esp_lcd_new_panel_*`** for **SSD1306** / **ST7789** when those profiles are active.
- **Console:** **`sdkconfig.defaults`** forces **USB Serial/JTAG** as primary console (see **§7.1**). Root **`CMakeLists.txt`** enables **`MINIMAL_BUILD`** and **`unset(COMPONENTS CACHE)`** so the component list stays minimal.
- Keep **v1** feature set **tight**; defer **logging**, **touch**, **screenshots**, **NVS profiles**, and **dynamic pin reassignment** to future revisions if needed.

---

## 18. Acceptance criteria (v1)

| ID | Criterion |
|----|-----------|
| AC-1 | Firmware builds with ESP-IDF for **esp32c3** target; **ESP-IDF `esp_lcd`** + **§5.5** approved sources only (registry-managed drivers **or** documented **ST7735** fork in **`components/`**; no ad-hoc vendor code) |
| AC-2 | Serial **hookup** + **safe boot sequence** before aggressive driving |
| AC-3 | At least **six SPI** profiles and **I2C display** path operational; **boot** runs **I2C** probe + **identify** (**§3.1**), **SPI** if no match (**§3.3** timing reference) |
| AC-4 | **Alignment** nudge keys per §9 (**`wasd`**); **`O`** / **comma** / guided **`.`** mapping per **§7.3.5** |
| AC-5 | **Backlight** 0–100% step 5 on TFT; **I2C** path skips BL when N/A |
| AC-6 | **Standard test sequence** + **individual** tests |
| AC-7 | **Peak SPI** ladder + **y/n** + rollback + stored value for **§13** |
| AC-8 | **§13** print config **omits** irrelevant fields per §13 |
| AC-9 | **SPI** wrong profile → **retry** (**§4.2**–**§4.3**); **fatal** wiring faults → **abort** + tips (**§14**) |
| AC-10 | **No NVS**; **no** touch/screenshot/logging pipeline in v1 |
| AC-11 | Default **GPIO** map follows **§2.1.2** (display header order ↔ GVS), with **strapping/SPI2** overrides documented |
| AC-12 | **Appendix B** = firmware **board pin** manifest (**§16.4**); **no** runtime pin changes |
| AC-13 | **I2C** GPIOs **≠** any **SPI** GPIO (**Appendix B** / **§16.4** point 5) |
| AC-14 | **Guided** workflow (**§7.3**): **.** / **Enter** (Next) / **comma** (Previous) / **`R`** (Restart); **`E`** (or documented key) to expert menu; **§13** printed at **end** of guided run; expert **print config** on demand (**`O`** or menu) |

---

## 19. Document history

| Version | Date | Notes |
|---------|------|-------|
| 0.1 | — | Initial comprehensive spec from stakeholder interviews |
| 0.2 | — | Host board locked to **Tenstar Robot ESP32-C3 Super Mini**; §2.1 GPIO table; `docs/hardware/C3-Mini/` assets; strapping notes; hookup output rules |
| 0.3 | — | **§2.1.1** — **ESP32-C3 Expansion Board** GVS layout, top/bottom GPIO silkscreen, power block, hookup rules |
| 0.4 | — | **§5.3** — SPI2 only for external SPI; pins assigned in firmware; **§6.1** — **Enter** to continue after hookup |
| 0.5 | — | **§2.1.2** — wire-order–first GPIO map aligned to typical cheap SPI header; contiguous GVS; strapping override |
| 0.6 | — | **§16.4** + **Appendix B** — hard-coded canonical pin manifest; repeatable hookup |
| 0.7 | — | **I2C** on **GPIO 0/1** — disjoint from SPI **5–10**; **§16.4** exclusivity rule |
| 0.8 | — | **§3.1** — boot **I2C scan** first; **SPI** if no I2C display; **§6.4**; workflow **§15** |
| 0.9 | — | **§3.1** — I2C **not** assumed OLED; **probe + ID**; **§3.3** scan timing reference |
| 1.0 | — | **§5.4** — mandatory **ESP-IDF `esp_lcd`**; **SSD1306** / **ST7789** vendor panels when applicable |
| 1.1 | — | **§5.5** — pinned **ESP Component Registry** drivers; **`main/idf_component.yml`**, **`managed_components/`**, **`dependencies.lock`** |
| 1.2 | — | **§4.3** / **§14** — **SPI** chipset **retry loop** on failure; **fatal** vs **recoverable** |
| 1.3 | — | **§4.2** — **SPI** two-step: **chipset** then **preset or custom size**; **§4.4** I2C renumbered |
| 1.4 | — | **§7.3** — guided workflow, **Next/Previous/Restart**, expert escape, **`D`** at guided end; **§7.3.5** **`D`** collision |
| 1.5 | — | **Appendix B** — SPI **MOSI/RST** moved to **GPIO 20/21** (avoid strapping **8/9**); **§2.1.2** mapping note |
| 1.6 | — | **§7.1** — **USB Serial/JTAG** primary console; **§5.5** — **ST7735** vendored in **`components/`**; **§17** — build/console notes |
| 1.7 | — | **§7.2** — case-insensitive letters; **§4.2** / G3 — **M** manual vs **T** try-sequence; **§7.3.5** / **§13** — handoff key **`O`** ( **`P`** = Previous); **§9** — nudge down **`V`** only |
| 1.8 | — | **§7.3** — guided shell: **`E`** Expert (not **`M`**, reserved for G3 SPI manual); **`G`** stage reminder; **G2** copy clarifies **N** then **G3** before **`M`**/**`T`**; per-menu key reuse allowed (**§7.3.3**) |
| 1.9 | — | **§7** — post-identity **Advanced** option **1** from overview: resume at **panel setup** without re-showing overview; **`guided_stage_meta`** invalid index → explicit “Unknown step” (not silent fallback) |
| 1.10 | — | **§3.2.1** — vendor init log vs chip ID; **§4.2** — try-sequence **resolution filter**, **ascending-area** trial order, **Phase 0b** tri-state (**Next** / format / OK), **secondaries**, **orientation-up** before **§4.2.1**, **magenta** **y/n/q**, pass **c/q**; **§4.2.1** — full clear, **session-mapped** size pattern colours, inset + double frame, scaled strips; **§7.3.2** G3 row |
| 1.11 | 2026-04-05 | **§20** — open-items table refreshed; README documents terminal echo (single-key vs line entry), SPI ladder + stress defaults; `handoff_print` module rename (was misleading `handoff_stub`) |
| 1.12 | 2026-04-05 | **§4.2** / **§4.2.1** — ST7735 **GRAM** sizes (e.g. 130×160, 132×162), try-sequence trials, **interactive** gap on orientation-up + size check, **`[`/`]`** / **`,`/`.`** memory **WxH** nudge on size check; **§9.1** cross-ref; README keys summary |
| 1.13 | 2026-04-05 | **Phase 2 serial UX** — global **`wasd`** gap, **comma** / **`.`** / **Enter** nav in stage submenus and guided shell (**`.`** Next, **comma** Previous); **`o`**/**`x`**/**`y`**/**`i`** orientation; ST7735 height nudge **`(`/`)`** (frees **comma**/**`.`**); **§8.4**; **§9.2**/**§9.5**/**§10.1**/**§7.3.5**/**AC-4**/**AC-14** updated; README v1.13 |
| 1.14 | 2026-04-05 | **Phase 5 I2C OLED** — G3 **1**/ **2** driver menu (128×64), **SH1106** default **gap_col 2**, RGB565 shadow → 1 bpp flush; handoff **esp_lcd** I2C + Arduino note; G4 TOP marker on I2C; **§4.4** |
| 1.15 | 2026-04-05 | **Orientation UX (VanMate):** **`R`**/**`W`**/**`S`**/**`A`**/**`D`**/**`I`** replace **`o`**/**`x`**/**`y`**/**`i`** on arrow-before-size + G4; **no** gap on orientation screen; **full preset GRAM** clear for orientation probe; **`panel_hw_sync_orientation_up_probe`**; operator copy **secondary color test** / **Step 1–2**; **README** + **§4.2**/**§4.2.1**/**§7.3.2**/**§7.3.5**/**§8.4** |

---

## 20. Open items (maintenance)

**Satisfied (reference only):** GPIO manifest is **fixed** — **Appendix B** and `main/board_pins.h` must stay in lockstep (**§16.4**). **README** links to [`docs/hardware/README.md`](docs/hardware/README.md), which indexes **`docs/hardware/C3-Mini/`** photos and diagrams. Default **SPI peak ladder** and stress behaviour are summarized in **README** (authoritative values in firmware, `display_stage_g8()` in `main/stage_patterns.c`).

| Item | Owner / notes |
|------|----------------|
| **SPI chip list** (~six profiles, optional ILI9163) | Implementer — presets / drivers in `spi_presets.c`, `panel_hw.c`; follow **§4.1** / **§5.5** |
| **Serial UX cleanup** (optional) | Shared stage strings, `!`/`@` during line entry — see `docs/ux_serial_flow_plan.md`, `docs/architecture_review.md` |
| **Branding** (SPI try-sequence success graphic) | Product decision — `brand_turnip_assets.c`; document or gate if non-default |

---

## Appendix A — Board name string (for serial / §13 output)

Use consistent branding in user-facing text:

- **Full stack:** `Tenstar Robot ESP32-C3 Super Mini` + `ESP32-C3 Expansion Board`
- **Module only:** `Tenstar Robot ESP32-C3 Super Mini`
- **Short (if space limited):** `ESP32-C3 Super Mini + Expansion (Tenstar Robot)`

---

## Appendix B — Canonical pin manifest (authoritative)

**Status:** Default **project** pinout for **Tenstar Robot ESP32-C3 Super Mini** + **ESP32-C3 Expansion Board**. **Firmware** must define these values in **`board_pins.h`** (or equivalent); this table **must** match that file.

**SPI TFT (typical 8-pin header order after GND/VCC — wire signals to match §2.1.2):**

| Canonical role | GPIO | Top GVS silkscreen (yellow column) | Notes |
|----------------|------|--------------------------------------|--------|
| **SCK** | **10** | **10** | SPI2 clock |
| **MOSI** | **20** | **20** | SPI2 MOSI — **not** GPIO **9** (strapping) |
| **RST** | **21** | **21** | Panel reset — **not** GPIO **8** (strapping) |
| **DC** | **7** | **7** | Data / command |
| **CS** | **6** | **6** | Chip select |
| **BL** | **5** | **5** | Backlight (PWM) |
| **MISO** | — | — | **Not connected** on typical cheap TFT (RDID N/A) |

**I2C display** (use when **no** SPI TFT is connected — **one bus at a time**):

| Canonical role | GPIO | Expansion (yellow column) | Notes |
|----------------|------|---------------------------|--------|
| **SDA** | **0** | Bottom strip, **0** | **Not** in SPI set **5, 6, 7, 10, 20, 21**; bottom row vs SPI on **top** |
| **SCL** | **1** | Bottom strip, **1** | Same — **maximize** harness separation from SPI |

**I2C vs SPI pin policy:** **I2C GPIOs must not share** any SPI GPIO (**5, 6, 7, 10, 20, 21**). The **0 / 1** choice keeps the **I2C** harness on the **bottom** GVS block (columns **0–1**) while the **SPI TFT** harness uses the **top** block (columns **5, 6, 7, 10, 20, 21**), reducing **tangles** and wrong-pin risk. **GPIO0** is a **strapping** pin — verify boot/flash with the **I2C display** connected (I2C idle **high** is typical).

**Power (not GPIO):** **GND** ↔ any board **GND**; display **VCC** ↔ **3V3** (or **5V** only if the module requires it) per **§2.1.1**.

**Vendor label cheat-sheet (always map to role above):**

| Misleading silk | Use as |
|-----------------|--------|
| SCL | **SCK** → GPIO **10** |
| SDA / SDI | **MOSI** → GPIO **20** |
| RES | **RST** → GPIO **21** |
| RS / A0 | **DC** → GPIO **7** |
| LED / BLK | **BL** → GPIO **5** |

**Revision rule:** If **strapping**, **SPI2 routing**, or bench testing requires a change, **edit Appendix B and `board_pins.h` in the same change**; re-print hookup text from the manifest **only**.

---

*End of specification.*
