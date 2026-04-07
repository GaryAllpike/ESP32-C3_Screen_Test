/* Version: 0.9 (Pre-Release) | JBG Discovery Framework */
#include "display_stages.h"
#include "console_text.h"
#include "display_recovery.h"
#include "panel_hw.h"
#include "serial_menu.h"
#include "session.h"
#include "ui_colors.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>

static void panel_apply_gap_orientation_invert(const test_session_t *s)
{
    panel_hw_apply_gap(s);
    panel_hw_apply_orientation(s);
    panel_hw_apply_invert(s);
}

static void panel_redraw_for_g5_i2c_only(const test_session_t *s)
{
    panel_apply_gap_orientation_invert(s);
    if (panel_hw_is_i2c()) {
        (void)panel_hw_fill_mono(0x00);
    }
}

static void panel_redraw_for_g5_spi18_fallback(const test_session_t *s)
{
    panel_apply_gap_orientation_invert(s);
    if (panel_hw_is_spi()) {
        (void)panel_hw_draw_g5_alignment_pattern(s);
    }
}

/* Phase 6 — R/G/B/M band starts at 0,20,40,60 px from GRAM origin. */
static int rg_bm_key_to_band_start(int k)
{
    if (k == 'r') {
        return 0;
    }
    if (k == 'g') {
        return 20;
    }
    if (k == 'b') {
        return 40;
    }
    if (k == 'm') {
        return 60;
    }
    return -1;
}

/* Nested preset colours: M, C, G, R → width / height for right / bottom bezel quiz. */
static uint16_t extent_color_key_to_width(int k)
{
    if (k == 'm') {
        return 240;
    }
    if (k == 'c') {
        return 135;
    }
    if (k == 'g') {
        return 128;
    }
    if (k == 'r') {
        return 128;
    }
    return 0;
}

static uint16_t extent_color_key_to_height(int k)
{
    if (k == 'm') {
        return 320;
    }
    if (k == 'c') {
        return 240;
    }
    if (k == 'g') {
        return 160;
    }
    if (k == 'r') {
        return 128;
    }
    return 0;
}

static void clamp_u16(uint16_t *v, uint16_t lo, uint16_t hi)
{
    if (*v < lo) {
        *v = lo;
    }
    if (*v > hi) {
        *v = hi;
    }
}

bool display_stage_g5(test_session_t *s)
{
    console_clear_screen();
    if (!panel_hw_panel_ready()) {
        printf("--- STATE ---\n");
        printf("%-15s : %s\n", "Panel", "not ready");
        printf("\n--- NAV ---\n");
        printf("Use . or Enter in the main menu to continue.\n");
        return true;
    }

    int16_t g0c = s->gap_col, g0r = s->gap_row;

    if (panel_hw_is_spi() && panel_hw_bits_per_pixel() == 16) {
        session_print_display_truth(s, "Origin calibration (gap)");
        printf("\n--- STEP 1A — Ballpark ---\n");
        console_print_wrapped(
            "",
            "Look at the TOP and LEFT edges of the glass. Thick colour bands start at controller memory (0,0): "
            "along the top, columns 0–19 red, 20–39 green, 40–59 blue, 60+ magenta; same pattern down the left edge.\n");

        panel_apply_gap_orientation_invert(s);
        (void)panel_hw_draw_g5_origin_ballpark_rgb565(s);

        printf("\nWhich colour band touches the TOP bezel?  R G B M: ");
        int topk;
        for (;;) {
            topk = serial_read_menu_choice("rgbm", s);
            if (topk == SERIAL_KEY_APP_RESTART) {
                return false;
            }
            if (topk == SERIAL_KEY_DISPLAY_RECOVERED) {
                (void)panel_hw_draw_g5_origin_ballpark_rgb565(s);
                printf("\nWhich colour band touches the TOP bezel?  R G B M: ");
                continue;
            }
            if (rg_bm_key_to_band_start(topk) >= 0) {
                break;
            }
        }
        s->gap_row = (int16_t)rg_bm_key_to_band_start(topk);

        printf("\nWhich colour band touches the LEFT bezel?  R G B M: ");
        int leftk;
        for (;;) {
            leftk = serial_read_menu_choice("rgbm", s);
            if (leftk == SERIAL_KEY_APP_RESTART) {
                return false;
            }
            if (leftk == SERIAL_KEY_DISPLAY_RECOVERED) {
                (void)panel_hw_draw_g5_origin_ballpark_rgb565(s);
                printf("\nWhich colour band touches the LEFT bezel?  R G B M: ");
                continue;
            }
            if (rg_bm_key_to_band_start(leftk) >= 0) {
                break;
            }
        }
        s->gap_col = (int16_t)rg_bm_key_to_band_start(leftk);

        printf("\n--- STEP 1B — Caliper ---\n");
        console_print_wrapped(
            "",
            "Screen cleared to black. A white square at logical (0,0) with a black ‘F’ marks the origin. "
            "Use W A S D to nudge column/row gap until the square clips the top and left bezels. "
            ", reverts to gap values from the start of this step. Enter or . locks and continues.\n");

        int16_t g1b_c = s->gap_col, g1b_r = s->gap_row;

        console_cursor_save();
        for (;;) {
            panel_apply_gap_orientation_invert(s);
            (void)panel_hw_draw_g5_origin_probe_rgb565(s);
            console_cursor_restore_clear_below();
            printf("\n--- STATE ---\n");
            printf("%-15s : %d\n", "Gap column", (int)s->gap_col);
            printf("%-15s : %d\n", "Gap row", (int)s->gap_row);
            printf("\n--- NAV ---\n");
            printf("Key: ");
            int k = serial_read_menu_choice(STAGE_KEYS_G5_GAP, s);
            if (k == SERIAL_KEY_APP_RESTART) {
                return false;
            }
            if (k == SERIAL_KEY_DISPLAY_RECOVERED) {
                continue;
            }
            if (k == SERIAL_KEY_ENTER || k == '.') {
                printf("Origin (gap) saved.\n");
                display_recovery_snapshot(s);
                return true;
            }
            if (k == ',') {
                s->gap_col = g1b_c;
                s->gap_row = g1b_r;
                printf("Reverted gap to start of alignment step.\n");
                continue;
            }
            if (k == 'a') {
                s->gap_col--;
            } else if (k == 'd') {
                s->gap_col++;
            } else if (k == 'w') {
                s->gap_row--;
            } else if (k == 's') {
                s->gap_row++;
            }
        }
    }

    /* I2C or SPI 18 bpp: classic gap nudge (no RGB ballpark). */
    session_print_display_truth(s, "Picture alignment / gap");
    printf("\n--- CONTROLS ---\n");
    console_print_wrapped(
        "",
        "wasd nudges the framebuffer: A=left D=right W=up S=down (column / row gap).\n"
        "Enter or . saves; , reverts gap to values when you opened this screen.\n");

    console_cursor_save();
    for (;;) {
        if (panel_hw_is_i2c()) {
            panel_redraw_for_g5_i2c_only(s);
        } else {
            panel_redraw_for_g5_spi18_fallback(s);
        }
        console_cursor_restore_clear_below();
        printf("\n--- STATE ---\n");
        printf("%-15s : %d\n", "Gap column", (int)s->gap_col);
        printf("%-15s : %d\n", "Gap row", (int)s->gap_row);
        printf("\n--- NAV ---\n");
        printf("Key: ");
        int k = serial_read_menu_choice(STAGE_KEYS_G5_GAP, s);
        if (k == SERIAL_KEY_APP_RESTART) {
            return false;
        }
        if (k == SERIAL_KEY_DISPLAY_RECOVERED) {
            continue;
        }
        if (k == SERIAL_KEY_ENTER || k == '.') {
            printf("Alignment saved (session RAM).\n");
            display_recovery_snapshot(s);
            return true;
        }
        if (k == ',') {
            s->gap_col = g0c;
            s->gap_row = g0r;
            printf("Reverted gap for this screen.\n");
            continue;
        }
        if (k == 'a') {
            s->gap_col--;
        } else if (k == 'd') {
            s->gap_col++;
        } else if (k == 'w') {
            s->gap_row--;
        } else if (k == 's') {
            s->gap_row++;
        }
    }
}

bool display_stage_g6(test_session_t *s)
{
    console_clear_screen();
    if (!panel_hw_panel_ready()) {
        printf("--- STATE ---\n");
        printf("%-15s : %s\n", "Panel", "not ready");
        printf("\n--- NAV ---\n");
        printf("Use . or Enter in the main menu to continue.\n");
        return true;
    }

    uint16_t fbw = 0, fbh = 0;
    panel_hw_spi_fb_size(&fbw, &fbh);

    if (panel_hw_is_spi() && panel_hw_bits_per_pixel() == 16) {
        if (fbw == 0 || fbh == 0) {
            printf("--- STATE ---\n");
            printf("%-15s : %s\n", "Extents", "panel size unknown");
            return true;
        }

        session_print_display_truth(s, "Extents calibration");
        printf("\n--- STEP 2A — Ballpark ---\n");
        console_print_wrapped(
            "",
            "Nested rectangles from the logical top-left (after gap): largest magenta 240×320, then cyan 135×240, "
            "green 128×160, smallest red 128×128.\n"
            "Look at the RIGHT and BOTTOM bezels.\n"
            "Keys: M = magenta preset, C = cyan, G = green, R = red (smallest).\n");

        panel_apply_gap_orientation_invert(s);
        (void)panel_hw_draw_g6_extents_ballpark_rgb565(s);

        printf("\nWhich colour touches the RIGHT bezel?  M C G R: ");
        int rk;
        for (;;) {
            rk = serial_read_menu_choice("mcgr", s);
            if (rk == SERIAL_KEY_APP_RESTART) {
                return false;
            }
            if (rk == SERIAL_KEY_DISPLAY_RECOVERED) {
                (void)panel_hw_draw_g6_extents_ballpark_rgb565(s);
                printf("\nWhich colour touches the RIGHT bezel?  M C G R: ");
                continue;
            }
            if (extent_color_key_to_width(rk) > 0) {
                break;
            }
        }
        s->hor_res = extent_color_key_to_width(rk);
        {
            uint16_t cap = fbw;
            if (s->phys_w > 0) {
                cap = s->phys_w;
                if (cap > fbw) {
                    cap = fbw;
                }
            }
            clamp_u16(&s->hor_res, 1, cap);
        }

        printf("\nWhich colour touches the BOTTOM bezel?  M C G R: ");
        int bk;
        for (;;) {
            bk = serial_read_menu_choice("mcgr", s);
            if (bk == SERIAL_KEY_APP_RESTART) {
                return false;
            }
            if (bk == SERIAL_KEY_DISPLAY_RECOVERED) {
                (void)panel_hw_draw_g6_extents_ballpark_rgb565(s);
                printf("\nWhich colour touches the BOTTOM bezel?  M C G R: ");
                continue;
            }
            if (extent_color_key_to_height(bk) > 0) {
                break;
            }
        }
        s->ver_res = extent_color_key_to_height(bk);
        {
            uint16_t cap = fbh;
            if (s->phys_h > 0) {
                cap = s->phys_h;
                if (cap > fbh) {
                    cap = fbh;
                }
            }
            clamp_u16(&s->ver_res, 1, cap);
        }

        printf("\n--- STEP 2B — Caliper ---\n");
        console_print_wrapped(
            "",
            "Black screen; bright 2 px lines sit just inside the bottom-right of your chosen logical size. "
            "A/D adjusts width (hor_res); W/S adjusts height (ver_res). "
            ", reverts to size from the start of this step. Enter or . locks.\n");

        uint16_t hb = s->hor_res, vb = s->ver_res;

        console_cursor_save();
        for (;;) {
            panel_apply_gap_orientation_invert(s);
            (void)panel_hw_draw_g6_extents_probe_rgb565(s, s->hor_res, s->ver_res);
            console_cursor_restore_clear_below();
            printf("\n--- STATE ---\n");
            printf("%-15s : %u\n", "hor_res", (unsigned)s->hor_res);
            printf("%-15s : %u\n", "ver_res", (unsigned)s->ver_res);
            printf("\n--- NAV ---\n");
            printf("Key: ");
            int k = serial_read_menu_choice(STAGE_KEYS_G6_EXTENTS, s);
            if (k == SERIAL_KEY_APP_RESTART) {
                return false;
            }
            if (k == SERIAL_KEY_DISPLAY_RECOVERED) {
                continue;
            }
            if (k == SERIAL_KEY_ENTER || k == '.') {
                printf("Extents saved (session RAM).\n");
                break;
            }
            if (k == ',') {
                s->hor_res = hb;
                s->ver_res = vb;
                printf("Reverted hor_res / ver_res to start of extents step.\n");
                continue;
            }
            if (k == 'a') {
                if (s->hor_res > 1) {
                    s->hor_res--;
                }
            } else if (k == 'd') {
                uint16_t maxw = fbw;
                if (s->phys_w > 0 && s->phys_w < maxw) {
                    maxw = s->phys_w;
                }
                if (s->hor_res < maxw) {
                    s->hor_res++;
                }
            } else if (k == 'w') {
                if (s->ver_res > 1) {
                    s->ver_res--;
                }
            } else if (k == 's') {
                uint16_t maxh = fbh;
                if (s->phys_h > 0 && s->phys_h < maxh) {
                    maxh = s->phys_h;
                }
                if (s->ver_res < maxh) {
                    s->ver_res++;
                }
            }
        }

        /* Backlight (SPI TFT only) */
        session_print_display_truth(s, "Backlight");
        printf("\n--- CONTROLS ---\n");
        printf("%-15s : %s\n", "W / S", "brighter / dimmer (5% steps)");
        printf("%-15s : %s\n", ",", "revert to level when you opened this screen");
        printf("%-15s : %s\n", "Enter / .", "done");

        uint8_t b0 = s->backlight_pct;

        console_cursor_save();
        for (;;) {
            console_cursor_restore_clear_below();
            printf("\n--- STATE ---\n");
            printf("%-15s : %u\n", "Backlight %", (unsigned)s->backlight_pct);
            printf("\n--- NAV ---\n");
            printf("Key: ");
            panel_hw_set_backlight_pct(s);
            int k = serial_read_menu_choice(STAGE_KEYS_G6_BL, s);
            if (k == SERIAL_KEY_APP_RESTART) {
                return false;
            }
            if (k == SERIAL_KEY_DISPLAY_RECOVERED) {
                continue;
            }
            if (k == SERIAL_KEY_ENTER || k == '.') {
                display_recovery_snapshot(s);
                return true;
            }
            if (k == ',') {
                s->backlight_pct = b0;
                continue;
            }
            if (k == 'w') {
                if (s->backlight_pct < 100) {
                    s->backlight_pct = (uint8_t)(s->backlight_pct + 5);
                }
            } else if (k == 's') {
                if (s->backlight_pct >= 5) {
                    s->backlight_pct = (uint8_t)(s->backlight_pct - 5);
                } else {
                    s->backlight_pct = 0;
                }
            }
        }
    }

    if (!panel_hw_is_spi()) {
        printf("--- STATE ---\n");
        printf("%-15s : %s\n", "Extents / backlight", "N/A (not SPI TFT)");
        printf("\n--- NAV ---\n");
        printf("Use . or Enter in the main menu to continue.\n");
        return true;
    }

    /* SPI 18 bpp: backlight only */
    printf("--- STATE ---\n");
    printf("%-15s : %s\n", "Extents", "N/A (18 bpp — use panel setup size)");
    session_print_display_truth(s, "Backlight");
    printf("\n--- CONTROLS ---\n");
    printf("%-15s : %s\n", "W / S", "brighter / dimmer (5% steps)");
    printf("%-15s : %s\n", ",", "revert to level when you opened this screen");
    printf("%-15s : %s\n", "Enter / .", "done");

    uint8_t b0 = s->backlight_pct;

    console_cursor_save();
    for (;;) {
        console_cursor_restore_clear_below();
        printf("\n--- STATE ---\n");
        printf("%-15s : %u\n", "Backlight %", (unsigned)s->backlight_pct);
        printf("\n--- NAV ---\n");
        printf("Key: ");
        panel_hw_set_backlight_pct(s);
        int k = serial_read_menu_choice(STAGE_KEYS_G6_BL, s);
        if (k == SERIAL_KEY_APP_RESTART) {
            return false;
        }
        if (k == SERIAL_KEY_DISPLAY_RECOVERED) {
            continue;
        }
        if (k == SERIAL_KEY_ENTER || k == '.') {
            display_recovery_snapshot(s);
            return true;
        }
        if (k == ',') {
            s->backlight_pct = b0;
            continue;
        }
        if (k == 'w') {
            if (s->backlight_pct < 100) {
                s->backlight_pct = (uint8_t)(s->backlight_pct + 5);
            }
        } else if (k == 's') {
            if (s->backlight_pct >= 5) {
                s->backlight_pct = (uint8_t)(s->backlight_pct - 5);
            } else {
                s->backlight_pct = 0;
            }
        }
    }
}
