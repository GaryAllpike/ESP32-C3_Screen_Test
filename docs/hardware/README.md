# Hardware reference — host board

This folder documents the **Tenstar Robot ESP32-C3 Super Mini** and the **ESP32-C3 Expansion Board**, the reference stack for the display test firmware described in **`SPEC.md`** (§2.1, §2.1.1, §16).

## Contents

| Path | Description |
|------|-------------|
| **`C3-Mini/`** | Photos (`C3-Mini_front.jpg` = module **on** expansion board, `C3-Mini_back.jpg` = module pin labels, `C3-Mini_dev_board.jpg` = expansion alone) and diagrams (`*.webp` pinout, schematic, pin use) |

- **Module GPIO** table: **`SPEC.md` §2.1**  
- **Expansion board** (yellow GVS columns, **VCC1** / **VCC2**, bottom-right **3V3/GND/5V**): **`SPEC.md` §2.1.1**  
- **Tangle-free wiring** (GPIO order vs typical SPI module header): **`SPEC.md` §2.1.2**

When assigning SPI/I2C pins in firmware, follow **ESP32-C3 strapping** constraints (see **`SPEC.md` §6.2**, **Appendix B**). This project’s **SPI TFT** wiring avoids **GPIO8/GPIO9** for MOSI/RST; **I2C** still uses **GPIO0** (SDA), which has boot strapping behavior — keep wiring stable per the spec.
