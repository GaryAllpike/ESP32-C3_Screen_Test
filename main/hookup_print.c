#include "hookup_print.h"
#include "board_pins.h"
#include "console_text.h"
#include <stdio.h>

void hookup_print_wiring_pinout_only(void)
{
    printf("\nBoard: Tenstar Robot ESP32-C3 Super Mini + ESP32-C3 Expansion Board\n");
    printf("Connect ONE display. Use GVS columns labeled with GPIO numbers.\n\n");

    printf("SPI TFT (typical 8-pin header after GND/VCC) — top strip:\n");
    printf("  GND  -> expansion GND (black row or power block)\n");
    printf("  VCC  -> 3V3 (or 5V only if module requires it)\n");
    printf("  SCK  -> GPIO %d (column %d)\n", BOARD_DISPLAY_SPI_SCK, BOARD_DISPLAY_SPI_SCK);
    printf("  MOSI -> GPIO %d (column %d)\n", BOARD_DISPLAY_SPI_MOSI, BOARD_DISPLAY_SPI_MOSI);
    printf("  RST  -> GPIO %d (column %d)\n", BOARD_DISPLAY_SPI_RST, BOARD_DISPLAY_SPI_RST);
    printf("  DC   -> GPIO %d (column %d)\n", BOARD_DISPLAY_SPI_DC, BOARD_DISPLAY_SPI_DC);
    printf("  CS   -> GPIO %d (column %d)\n", BOARD_DISPLAY_SPI_CS, BOARD_DISPLAY_SPI_CS);
    printf("  BL   -> GPIO %d (column %d)\n", BOARD_DISPLAY_SPI_BL, BOARD_DISPLAY_SPI_BL);
    printf("  MISO -> NC on typical modules\n\n");

    printf("I2C display — bottom strip (disjoint from SPI):\n");
    printf("  SDA -> GPIO %d (column %d)\n", BOARD_DISPLAY_I2C_SDA, BOARD_DISPLAY_I2C_SDA);
    printf("  SCL -> GPIO %d (column %d)\n", BOARD_DISPLAY_I2C_SCL, BOARD_DISPLAY_I2C_SCL);
    printf("  GND / VCC per module\n\n");
}

void hookup_print_instructions(void)
{
    printf("\n");
    printf("======== ESP32-C3 display test — wiring ========\n\n");
    console_print_wrapped(
        "",
        "Blind recovery any time after the firmware runs:  !  restarts from this hookup.  "
        "@  restores the last known-good display after a snapshot.\n");
    printf("\n");
    hookup_print_wiring_pinout_only();

    console_print_wrapped(
        "",
        "Strapping: ESP32-C3 uses GPIO 0, 2, 8, and 9 for boot. This SPI TFT map avoids 8 and 9; "
        "I2C uses 0 and 1. Verify flash and boot strapping with your harness if needed.\n");
    printf("==================================================================\n\n");
}
