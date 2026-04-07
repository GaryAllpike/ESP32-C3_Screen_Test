/* Version: 0.9 (Pre-Release) | JBG Discovery Framework */
#include "display_stages.h"
#include "console_text.h"
#include "display_recovery.h"
#include "panel_hw.h"
#include "panel_probes.h"
#include "serial_menu.h"
#include "session.h"
#include "ui_colors.h"
#include <stdio.h>

static void panel_apply_gap_orientation_invert(const test_session_t *s)
{
    panel_hw_apply_gap(s);
    panel_hw_apply_orientation(s);
    panel_hw_apply_invert(s);
}

static void panel_redraw_for_g4(const test_session_t *s)
{
    if (panel_hw_is_spi() && panel_hw_bits_per_pixel() == 16) {
        panel_hw_sync_orientation_up_probe(s);
    } else {
        panel_apply_gap_orientation_invert(s);
        if (panel_hw_is_spi() || panel_hw_is_i2c()) {
            (void)panel_hw_draw_top_marker(s);
        }
    }
}

bool stage_2_run_orientation(test_session_t *s)
{
    console_clear_screen();
    if (!panel_hw_panel_ready()) {
        printf("--- STATE ---\n");
        printf("%-15s : %s\n", "Panel", "not ready");
        printf("\n--- NAV ---\n");
        printf("Use . or Enter in the main menu to continue.\n");
        return true;
    }
    session_print_display_truth_no_orient(s, "Orientation / TOP marker");
    printf("\n--- CONTROLS ---\n");
    console_print_wrapped(
        "",
        "SPI RGB565: TOP band toward top of glass; black F on white at origin reads normally.\n"
        "R rotate 90°; A / D toggle mirror X; W / S toggle mirror Y; I invert.\n"
        ", reverts this screen to values when you opened it. Enter or . confirms and saves.\n"
        "Other buses: TOP band at top of glass (same keys except R/W/S/A/D apply to orientation).\n"
        "Picture alignment (next step) is where you nudge column/row offset after rotation is locked.\n");
    console_cursor_save();

    uint8_t r0 = s->rot_quarter;
    bool mx0 = s->mirror_x, my0 = s->mirror_y;
    bool inv0 = s->inv_en;

    for (;;) {
        panel_redraw_for_g4(s);
        console_cursor_restore_clear_below();
        printf("\n--- STATE ---\n");
        printf("%-15s : %u\n", "Rotation", (unsigned)s->rot_quarter);
        printf("%-15s : %d\n", "Mirror X", s->mirror_x ? 1 : 0);
        printf("%-15s : %d\n", "Mirror Y", s->mirror_y ? 1 : 0);
        printf("%-15s : %d\n", "Invert", s->inv_en ? 1 : 0);
        printf("\n--- NAV ---\n");
        printf("Key: ");
        int k = serial_read_menu_choice(STAGE_KEYS_G4_ORIENT, s);
        if (k == SERIAL_KEY_APP_RESTART) {
            return false;
        }
        if (k == SERIAL_KEY_DISPLAY_RECOVERED) {
            continue;
        }
        if (k == SERIAL_KEY_ENTER || k == '.') {
            printf("Orientation saved.\n");
            /* Leave controller gap at (0,0) on the wire so Stage 3 silicon probe sees absolute GRAM; session gap unchanged. */
            panel_hw_spi_clear_hardware_gap();
            display_recovery_snapshot(s);
            return true;
        }
        if (k == ',') {
            s->rot_quarter = r0;
            s->mirror_x = mx0;
            s->mirror_y = my0;
            s->inv_en = inv0;
            panel_hw_set_orientation(s);
            panel_hw_set_inversion(s);
            panel_redraw_for_g4(s);
            printf("Reverted orientation for this screen.\n");
            continue;
        }
        if (k == 'r' || k == 'a' || k == 'd' || k == 'w' || k == 's' || k == 'i') {
            panel_probes_g4_dispatch_orientation_key(s, k);
        }
    }
}
