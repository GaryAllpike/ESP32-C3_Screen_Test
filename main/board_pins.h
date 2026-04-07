/*
 * Canonical display pin map — must match SPEC.md Appendix B.
 * Tenstar Robot ESP32-C3 Super Mini + ESP32-C3 Expansion Board.
 * SPI signals avoid GPIO 8 and 9 (ESP32-C3 strapping — §6.2).
 * Do not change GPIOs without updating SPEC.md in the same commit.
 */

#pragma once

/* SPI TFT (SPI2) — typical 8-pin module: GND, VCC, then SCK…BL per SPEC §2.1.2 */
#define BOARD_DISPLAY_SPI_SCK   10
#define BOARD_DISPLAY_SPI_MOSI  20
#define BOARD_DISPLAY_SPI_MISO  (-1)  /* NC on typical cheap TFT */
#define BOARD_DISPLAY_SPI_RST   21
#define BOARD_DISPLAY_SPI_DC     7
#define BOARD_DISPLAY_SPI_CS     6
#define BOARD_DISPLAY_SPI_BL     5

/* I2C display — use when SPI TFT not wired (one display at a time; not assumed OLED — SPEC §4.4).
 * Pins 0/1: disjoint from SPI and bottom GVS vs SPI top strip. */
#define BOARD_DISPLAY_I2C_SDA    0
#define BOARD_DISPLAY_I2C_SCL    1
