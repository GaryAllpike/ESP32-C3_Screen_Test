#include "display_stages.h"
#include "console_text.h"
#include "display_recovery.h"
#include "esp_err.h"
#include "panel_hw.h"
#include "serial_menu.h"
#include "session.h"
#include "ui_colors.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <inttypes.h>
#include <stdio.h>

static void stress_spi_pattern(const test_session_t *s)
{
    for (int i = 0; i < 24; i++) {
        (void)panel_hw_fill_rgb565((uint16_t)((i & 1) ? ui_color_alert(s) : ui_color_primary_b(s)));
        vTaskDelay(pdMS_TO_TICKS(40));
    }
}

bool display_stage_g10(test_session_t *s)
{
    console_clear_screen();
    if (!panel_hw_panel_ready()) {
        printf("--- STATE ---\n");
        printf("%-15s : %s\n", "Panel", "not ready");
        return true;
    }
    session_print_display_truth(s, "Batch calibration check");
    display_recovery_snapshot(s);
    printf("\n--- CONTROLS ---\n");
    printf("%-15s : %s\n", "Panel", "1 px white border on logical edges; ~50% gray inside (I2C: dither)");
    printf("%-15s : %s\n", "Enter", "Calibration OK — go to test patterns");
    printf("%-15s : %s\n", "r", "Slight shift — return to origin/gap step for this unit");
    printf("\n");

    esp_err_t dr = panel_hw_draw_batch_verification_overlay_rgb565(s);
    if (dr == ESP_ERR_NOT_SUPPORTED) {
        printf("(Border overlay is RGB565-only on this panel; use Enter or r as needed.)\n\n");
    } else if (dr != ESP_OK) {
        printf("Draw failed: %s\n\n", esp_err_to_name(dr));
    }

    console_cursor_save();
    for (;;) {
        console_cursor_restore_clear_below();
        printf("Enter = continue to test patterns   r = adjust G5   (!/@ as usual): ");
        int c = serial_read_menu_choice("\nr", s);
        if (c == SERIAL_KEY_APP_RESTART) {
            return false;
        }
        if (c == SERIAL_KEY_DISPLAY_RECOVERED) {
            (void)panel_hw_draw_batch_verification_overlay_rgb565(s);
            continue;
        }
        if (c == SERIAL_KEY_ENTER) {
            s->guided_override_next_stage = 7;
            return true;
        }
        if (c == 'r') {
            s->guided_override_next_stage = 5;
            return true;
        }
    }
}

bool display_stage_g8(test_session_t *s)
{
    console_clear_screen();
    if (!panel_hw_is_spi() || !panel_hw_panel_ready()) {
        printf("--- STATE ---\n");
        printf("%-15s : %s\n", "SPI speed", "N/A (SPI TFT only)");
        printf("\n--- NAV ---\n");
        printf("%-15s : %s\n", "Continue", ". or Enter in main menu");
        return true;
    }
    session_print_display_truth(s, "SPI speed check");
    display_recovery_snapshot(s);
    printf("\n--- CONTROLS ---\n");
    printf("%-15s : %s\n", "At each step", "watch the panel; y / n (Enter = y) after stress pattern");

    console_cursor_save();

    static const uint32_t ladder[] = {
        20 * 1000 * 1000,
        26 * 1000 * 1000,
        40 * 1000 * 1000,
        53 * 1000 * 1000,
        80 * 1000 * 1000,
    };

    uint32_t best = panel_hw_spi_pclk_hz();
    if (best == 0) {
        best = 20 * 1000 * 1000;
    }

    const int n_ladder = (int)(sizeof(ladder) / sizeof(ladder[0]));
    for (int i = 0; i < n_ladder; i++) {
retry_rung:
        uint32_t f = ladder[i];
        if (f <= best) {
            continue;
        }
        console_cursor_restore_clear_below();
        printf("\n--- STATE ---\n");
        printf("%-15s : %" PRIu32 " Hz\n", "Candidate PCLK", f);
        printf("\n--- NAV ---\n");
        printf("Stress pattern… OK? y/n (Enter=y): ");
        if (panel_hw_spi_set_pclk(s, f) != ESP_OK) {
            printf("Clock set failed.\n");
            break;
        }
        stress_spi_pattern(s);
        int c = serial_read_menu_choice_yesno(s);
        if (c == SERIAL_KEY_APP_RESTART) {
            return false;
        }
        if (c == SERIAL_KEY_DISPLAY_RECOVERED) {
            goto retry_rung;
        }
        if (c == 'y') {
            best = f;
        } else {
            printf("Rolling back to %" PRIu32 " Hz…\n", best);
            (void)panel_hw_spi_set_pclk(s, best);
            stress_spi_pattern(s);
            printf("Re-check at %" PRIu32 " Hz OK? y/n (Enter=y): ", best);
            c = serial_read_menu_choice_yesno(s);
            if (c == SERIAL_KEY_APP_RESTART) {
                return false;
            }
            if (c == SERIAL_KEY_DISPLAY_RECOVERED) {
                goto retry_rung;
            }
            if (c != 'y') {
                printf("Leaving PCLK at %" PRIu32 " Hz (last good).\n", (uint32_t)panel_hw_spi_pclk_hz());
            }
            break;
        }
    }

    s->peak_spi_hz = best;
    (void)panel_hw_spi_set_pclk(s, best);
    printf("\n--- STATE ---\n");
    printf("%-15s : %" PRIu32 " Hz\n", "Session peak SPI", best);
    return true;
}
