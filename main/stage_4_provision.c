/* Version: 0.9 (Pre-Release) | JBG Discovery Framework */
/*
 * Stage 4 (G9) — discovery complete: panel art + serial profile.
 * Chip-specific serial output is only in provision_print_profile_dispatch(); no chip checks here.
 */

#include "display_stages.h"
#include "console_text.h"
#include "provision_print.h"
#include "panel_hw.h"
#include "session.h"
#include "ui_colors.h"
#include <stdio.h>

bool stage_4_run_provision(test_session_t *s)
{
    console_clear_screen();
    printf("\n=== Discovery complete ===\n");

    if (!panel_hw_panel_ready()) {
        printf("Panel not ready — serial summary only.\n\n");
        provision_print_session_summary(s);
        return true;
    }

    if (panel_hw_is_spi() && panel_hw_bits_per_pixel() == 16) {
        (void)panel_hw_fill_rgb565(ui_color_bg(s));
        (void)panel_hw_draw_probe_marker_centred();
    } else if (panel_hw_is_i2c()) {
        (void)panel_hw_fill_mono(0x00);
    }

    printf("W: %u H: %u | X:%d Y:%d\n", (unsigned)s->phys_w, (unsigned)s->phys_h, (int)s->gap_col,
           (int)s->gap_row);

    provision_print_profile_dispatch(s);

    return true;
}
