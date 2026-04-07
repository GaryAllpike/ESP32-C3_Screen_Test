/* Version: 0.9 (Pre-Release) | JBG Discovery Framework */
/*
 * Stage 3 — Silicon compass + extent (SPI RGB565).
 *
 * Compass: operator quadrant → panel_mirror_t; panel_hw maps to driver MADCTL.
 * stage3_apply_silicon_compass() sets silicon_mirror, session_sync_mirror_from_silicon() for
 * mirror_x/y, phys_w/h, hor_res/ver_res, re-inits SPI, panel_hw_set_silicon_basis().
 * Post-compass: logical searchlight marker; @/# mirror tweaks sync orient/invert then redraw silicon safe square.
 */

#include "display_stages.h"
#include "display_recovery.h"
#include "panel_hw.h"
#include "serial_menu.h"
#include "session.h"
#include "spi_presets.h"
#include "esp_log.h"
#include <stdio.h>

static const char *TAG = "stage3_probe";

static panel_mirror_t stage3_compass_mirror_for_choice(int choice_ch)
{
    static const struct {
        int key;
        panel_mirror_t mirror;
    } rows[] = {
        { 'a', PANEL_MIRROR_XY },
        { 'b', PANEL_MIRROR_Y },
        { 'c', PANEL_MIRROR_X },
        { 'd', PANEL_MIRROR_NONE },
    };
    for (size_t i = 0; i < sizeof(rows) / sizeof(rows[0]); i++) {
        if (rows[i].key == choice_ch) {
            return rows[i].mirror;
        }
    }
    return PANEL_MIRROR_NONE;
}

static void stage3_apply_silicon_compass(test_session_t *s, int choice)
{
    panel_hw_session_set_silicon_mirror(s, stage3_compass_mirror_for_choice(choice));
    session_sync_mirror_from_silicon(s);
    spi_presets_chip_gram_max(s->spi_chip, &s->phys_w, &s->phys_h);
    s->hor_res = s->phys_w;
    s->ver_res = s->phys_h;
    uint32_t pc = s->spi_pclk_hz ? s->spi_pclk_hz : (20u * 1000u * 1000u);
    esp_err_t err = panel_hw_spi_init(s, s->spi_chip, s->hor_res, s->ver_res, pc);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "spi_init silicon geometry: %s", esp_err_to_name(err));
        return;
    }
    panel_hw_set_silicon_basis(s);
}

bool stage_3_run_probe(test_session_t *s)
{
    if (!panel_hw_is_spi() || panel_hw_bits_per_pixel() != 16) {
        return true;
    }

    int16_t g0c = s->gap_col, g0r = s->gap_row;
    uint16_t w0 = s->hor_res, h0 = s->ver_res;
    uint16_t pw0 = s->phys_w, ph0 = s->phys_h;
    panel_mirror_t sm0 = s->silicon_mirror;
    bool mx0 = s->mirror_x, my0 = s->mirror_y;
    uint8_t ext0 = s->silicon_extent_red_hits_right;

    s->silicon_extent_red_hits_right = 0;

    panel_hw_spi_clear_hardware_gap();
    serial_discard_buffered_console_input();

    esp_err_t probe_e = panel_hw_draw_silicon_probe(s);
    if (probe_e != ESP_OK) {
        ESP_LOGW(TAG, "silicon probe: %s", esp_err_to_name(probe_e));
    }

    int ch = 0;
    for (;;) {
        printf("\n--- SILICON COMPASS ---\n");
        printf("The White square is at the Hardware Origin (0,0).\n");
        printf("From the white square, which direction does the rest of the panel extend?\n");
        printf(" (A) Up and Left\n");
        printf(" (B) Up and Right\n");
        printf(" (C) Down and Left\n");
        printf(" (D) Down and Right\n");
        printf("Choice: ");
        ch = serial_read_menu_choice("\n" "abcd", s);
        if (ch == SERIAL_KEY_APP_RESTART) {
            return false;
        }
        if (ch == SERIAL_KEY_DISPLAY_RECOVERED) {
            panel_hw_spi_clear_hardware_gap();
            (void)panel_hw_draw_silicon_probe(s);
            continue;
        }
        if (ch == 'a' || ch == 'b' || ch == 'c' || ch == 'd') {
            break;
        }
    }

    stage3_apply_silicon_compass(s, ch);

    panel_hw_apply_gap(s);
    panel_hw_apply_orientation(s);
    panel_hw_apply_invert(s);

    for (;;) {
        esp_err_t ge = panel_hw_draw_marker_probe_rgb565(s);
        if (ge != ESP_OK) {
            ESP_LOGW(TAG, "marker probe: %s", esp_err_to_name(ge));
        }

        printf("\nOrigin set. Look at the 'F' in the white square.\n");
        printf("Is the 'F' oriented correctly and readable? (y/n)\n");
        printf("Choice: ");
        int yn = serial_read_menu_choice("\n" "yn", s);
        if (yn == SERIAL_KEY_APP_RESTART) {
            return false;
        }
        if (yn == SERIAL_KEY_DISPLAY_RECOVERED) {
            panel_hw_set_silicon_basis(s);
            panel_hw_apply_gap(s);
            panel_hw_apply_orientation(s);
            panel_hw_apply_invert(s);
            continue;
        }
        if (yn == 'y') {
            break;
        }
        if (yn != 'n') {
            continue;
        }

        for (;;) {
            printf("\n[@] Flip X (Mirror) | [#] Flip Y (Upside Down)\n");
            printf("Choice: ");
            int mk = serial_read_menu_choice("@#", s);
            if (mk == SERIAL_KEY_APP_RESTART) {
                return false;
            }
            if (mk == SERIAL_KEY_DISPLAY_RECOVERED) {
                panel_hw_set_silicon_basis(s);
                panel_hw_apply_gap(s);
                panel_hw_apply_orientation(s);
                panel_hw_apply_invert(s);
                break;
            }
            if (mk == '@') {
                s->mirror_x = !s->mirror_x;
                panel_hw_apply_orientation(s);
                panel_hw_apply_invert(s);
                (void)panel_hw_draw_silicon_probe(s);
                break;
            }
            if (mk == '#') {
                s->mirror_y = !s->mirror_y;
                panel_hw_apply_orientation(s);
                panel_hw_apply_invert(s);
                (void)panel_hw_draw_silicon_probe(s);
                break;
            }
        }
    }

    /* Full viewport black between marker phase and extent axes (buffer hygiene). */
    panel_hw_apply_gap(s);
    panel_hw_apply_orientation(s);
    panel_hw_apply_invert(s);
    (void)panel_hw_fill_rgb565(0x0000u);

    for (;;) {
        esp_err_t ext_e = panel_hw_draw_stage3_extent_probe_rgb565(s);
        if (ext_e != ESP_OK) {
            ESP_LOGW(TAG, "extent probe: %s", esp_err_to_name(ext_e));
        }

        printf("\n--- SILICON EXTENT ---\n");
        printf("With the origin mapped to the physical top-left, look at the RED line from the top-left corner.\n");
        printf("Does the Red line hit the right bezel?\n");
        printf(" (Y) Yes  (N) No\n");
        printf("Choice: ");
        int ext_ch = serial_read_menu_choice("\n" "yn", s);
        if (ext_ch == SERIAL_KEY_APP_RESTART) {
            return false;
        }
        if (ext_ch == SERIAL_KEY_DISPLAY_RECOVERED) {
            panel_hw_set_silicon_basis(s);
            panel_hw_apply_gap(s);
            panel_hw_apply_orientation(s);
            panel_hw_apply_invert(s);
            continue;
        }
        if (ext_ch == 'y' || ext_ch == 'n') {
            s->silicon_extent_red_hits_right = (uint8_t)ext_ch;
            /* Commit trial WxH from extent probe as authoritative silicon extent (provision). */
            s->phys_w = s->hor_res;
            s->phys_h = s->ver_res;
            break;
        }
    }

    if (s->gap_col != g0c || s->gap_row != g0r || s->hor_res != w0 || s->ver_res != h0 || s->phys_w != pw0 ||
        s->phys_h != ph0 || s->silicon_mirror != sm0 || s->mirror_x != mx0 || s->mirror_y != my0 ||
        s->silicon_extent_red_hits_right != ext0) {
        display_recovery_snapshot(s);
    }
    return true;
}
