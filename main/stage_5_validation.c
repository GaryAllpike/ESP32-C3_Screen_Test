/* Version: 0.9 (Pre-Release) | JBG Discovery Framework */
/*
 * Automated validation: checker → gradient cycle, then optional 360° probe-marker spin.
 */

#include "display_stages.h"
#include "console_text.h"
#include "display_recovery.h"
#include "panel_hw.h"
#include "serial_menu.h"
#include "session.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <inttypes.h>
#include <stdio.h>

#define STAGE5_VALIDATION_CYCLE_MS 2000

static void stage5_run_auto_visual_cycle(const test_session_t *s)
{
    (void)panel_hw_probe_draw_checkerboard(s, 1);
    vTaskDelay(pdMS_TO_TICKS(STAGE5_VALIDATION_CYCLE_MS));
    (void)panel_hw_probe_draw_checkerboard(s, 8);
    vTaskDelay(pdMS_TO_TICKS(STAGE5_VALIDATION_CYCLE_MS));
    (void)panel_hw_probe_draw_gradient(s);
    vTaskDelay(pdMS_TO_TICKS(STAGE5_VALIDATION_CYCLE_MS));
}

static void stage5_run_performance_spin_360(const test_session_t *s)
{
    if (!panel_hw_is_spi() || panel_hw_bits_per_pixel() != 16) {
        printf("360° probe-marker spin requires SPI RGB565.\n");
        return;
    }
    printf("360° rotation (1° per frame, ~15 ms each).\n");
    TickType_t t0 = xTaskGetTickCount();
    for (int ang = 0; ang < 360; ang++) {
        esp_err_t e = panel_hw_probe_draw_turnip(s, ang);
        if (e != ESP_OK) {
            printf("Spin: %s\n", esp_err_to_name(e));
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(15));
    }
    printf("Spin done — ~%" PRIu32 " ms.\n", (uint32_t)pdTICKS_TO_MS(xTaskGetTickCount() - t0));
}

bool stage_5_run_validation_loop(test_session_t *s)
{
    console_clear_screen();
    if (!panel_hw_panel_ready()) {
        printf("--- STATE ---\n");
        printf("%-15s : %s\n", "Panel", "not ready");
        return true;
    }

    session_print_display_truth(s, "Validation (automated cycle)");
    display_recovery_snapshot(s);

    console_cursor_save();
    for (;;) {
        console_cursor_restore_clear_below();
        printf("\n--- Automated validation ---\n");
        printf("Running: 1 px checker → 8 px checker → gradient (2 s each)…\n");
        stage5_run_auto_visual_cycle(s);

        console_cursor_restore_clear_below();
        printf("\nVisuals steady and colors correct?\n");
        printf("  (Y) Continue to performance   (N) Retry cycle\n");
        printf("Choice: ");
        int c = serial_read_menu_choice("\n" "yn", s);
        if (c == SERIAL_KEY_APP_RESTART) {
            return false;
        }
        if (c == SERIAL_KEY_DISPLAY_RECOVERED) {
            continue;
        }
        if (c == 'y' || c == 'Y') {
            stage5_run_performance_spin_360(s);
            break;
        }
        if (c == 'n' || c == 'N') {
            continue;
        }
    }

    return true;
}

bool stage_5_validation_run(test_session_t *s)
{
    return stage_5_run_validation_loop(s);
}
