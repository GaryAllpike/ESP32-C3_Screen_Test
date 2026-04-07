/* Version: 0.9 (Pre-Release) | JBG Discovery Framework */
#include "display_stages.h"
#include "console_text.h"
#include "display_recovery.h"
#include "panel_hw.h"
#include "serial_menu.h"
#include "session.h"
#include "spi_presets.h"
#include "ui_colors.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <inttypes.h>
#include <stdio.h>

static const char *TAG = "disp_stage";

/* Bench hints: wrong size, offset, colour, clock vs wiring (width via console_text). */
static const char k_tips_st7735[] =
    "Many ST7735 modules differ by tab (green, red, or black) and 128x128 vs 128x160.\n"
    "Stray vertical stripes or garbage at the left/right edge are usually GRAM column offset, not wrong WxH: "
    "use gap / alignment after rotation is locked (not during Step 2 orientation).\n"
    "Stage 3 Silicon Compass sets controller GRAM extent (phys_w/h) and mirror basis; G6 extents refine visible hor_res/ver_res.\n"
    "Shifted or cropped image: try orientation, column/row gap, then wiring — not a hard-coded driver offset.\n"
    "Invert defaults on; if colours look wrong, try invert in the Orientation step.\n";

static const char k_tips_st7789[] =
    "240x240, 240x320, and 135x240 boards often use different internal GRAM offsets.\n"
    "Wrong preset may show a band or wrong active area; try another size or alignment.\n"
    "Firmware uses BGR; fix tint and mirror in Orientation before changing chip.\n";

static const char k_tips_ili9341[] =
    "Boards are often marked ILI9341 when the part behaves like ILI9342, or the reverse.\n"
    "If init works but geometry is wrong, try the other preset or custom width by height.\n"
    "Mirror or offset with correct colours: fix Orientation and alignment, not new wiring.\n";

static const char k_tips_ili9488[] =
    "ILI9488 uses 18-bit colour and usually needs a lower SPI clock than smaller RGB panels.\n"
    "Snow or flaky updates: keep SPI clock moderate in the speed check until stable.\n"
    "Same DC, CS, and RST wiring as other TFTs; wrong colours are usually orientation.\n";

static const char k_tips_gc9a01[] =
    "Round modules still use a rectangular window; nudge alignment if the circle is off.\n"
    "Invert is on by default; photo-negative image, try invert in Orientation.\n"
    "If only the ring or edges look wrong, try rotate and mirror in Orientation first.\n";

static const char k_tips_st7796[] =
    "ST7796 is often used on 320x480 SPI modules (similar footprint to ILI9488 but RGB565 flow differs).\n"
    "ESP-IDF may not ship a built-in ST7796 esp_lcd factory yet — use Manual for presets; panel init stays "
    "blocked until a vendor component is linked (see panel_hw TODO).\n"
    "Try 320x480 first; if the image is mirrored or offset, fix Orientation and gap before changing chip.\n";

static void print_spi_chip_bench_tips(session_spi_chip_t chip, const char *prefix)
{
    const char *t = NULL;
    switch (chip) {
    case SESSION_SPI_ST7735:
        t = k_tips_st7735;
        break;
    case SESSION_SPI_ST7789:
        t = k_tips_st7789;
        break;
    case SESSION_SPI_ILI9341:
        t = k_tips_ili9341;
        break;
    case SESSION_SPI_ILI9488:
        t = k_tips_ili9488;
        break;
    case SESSION_SPI_GC9A01:
        t = k_tips_gc9a01;
        break;
    case SESSION_SPI_ST7796:
        t = k_tips_st7796;
        break;
    default:
        break;
    }
    if (t) {
        console_print_wrapped(prefix, t);
    }
}

static bool parse_uu(const char *line, unsigned *a, unsigned *b)
{
    return sscanf(line, "%u %u", a, b) == 2;
}

static bool i2c_resolve_addr(test_session_t *s)
{
    if (s->i2c_addr_7bit != 0) {
        return true;
    }
    printf("No I2C address from scan — enter 7-bit addr in hex (e.g. 3C): ");
    char line[32];
    int n = serial_read_line_safe(line, sizeof(line), s);
    if (n == SERIAL_LINE_BOOT_RESTART) {
        return false;
    }
    unsigned v = 0;
    if (sscanf(line, "%x", &v) != 1 || v < 0x08 || v > 0x77) {
        printf("Invalid — using 0x3C.\n");
        s->i2c_addr_7bit = 0x3C;
        return true;
    }
    s->i2c_addr_7bit = (uint8_t)v;
    return true;
}

typedef struct {
    session_spi_chip_t chip;
    uint16_t w;
    uint16_t h;
    uint32_t pclk_hz;
    const char *hint;
    const char *trial_extra; /* NULL or one line after generic chip tips (try-sequence only) */
} spi_autotrial_t;

/* Ascending pixel area (smallest first) for bench setup — matches “assume smallest regular-use size” testing. */
static const spi_autotrial_t k_spi_trials[] = {
    { SESSION_SPI_ST7735, 128, 128, 20 * 1000 * 1000, "ST7735 128x128 (1.44\" round-ish)",
      "Smallest trial; if image cropped or wrong size, next trial or M manual for 128x160.\n" },
    { SESSION_SPI_ST7735, 128, 160, 20 * 1000 * 1000, "ST7735 128x160 (1.8\" \"tab\" modules)",
      "If offset, try 128x128 via M manual, or alignment after you accept.\n" },
    { SESSION_SPI_ST7735, 130, 160, 20 * 1000 * 1000, "ST7735 130x160 (130-wide memory)",
      "Between 128 and full GRAM; Stage 3 + G6 if edges still wrong.\n" },
    { SESSION_SPI_ST7735, 132, 162, 20 * 1000 * 1000, "ST7735 132x162 (common full GRAM)",
      "If 128x160 leaves garbage columns/rows at edges, this trial often matches the die.\n" },
    { SESSION_SPI_ST7789, 128, 160, 20 * 1000 * 1000, "ST7789 128x160 (some 1.8\" modules use this chip)",
      "If this looks wrong, try the ST7735 trial above or M manual for another geometry.\n" },
    { SESSION_SPI_ST7789, 135, 240, 20 * 1000 * 1000, "ST7789 135x240 (bar / Phone-style)",
      "Bar modules: wrong rotation is common; fix Orientation before trying another chip.\n" },
    { SESSION_SPI_ST7789, 240, 240, 20 * 1000 * 1000, "ST7789 240x240 (square IPS)",
      "Common square IPS: if edges look wrong, try 240x320 or 135x240 via M manual.\n" },
    { SESSION_SPI_GC9A01, 240, 240, 20 * 1000 * 1000, "GC9A01 240x240 (round)",
      "Round glass: nudge alignment if the active circle is not centred.\n" },
    { SESSION_SPI_ILI9341, 240, 320, 20 * 1000 * 1000, "ILI9341 / ILI9342 (common 2.4–3.2\" 240x320)", NULL },
    { SESSION_SPI_ILI9488, 320, 480, 10 * 1000 * 1000, "ILI9488 320x480 (3.5\" class)",
      "Large panel: if the bus glitches, avoid max SPI speed until stable.\n" },
};

typedef enum {
    SPI_P0B_OK = 0,
    SPI_P0B_FAIL_TRIAL = 1,
    SPI_P0B_APP_RESTART = 2,
} spi_phase0b_result_t;

typedef enum {
    MANUAL_MAP_DONE = 0,
    MANUAL_MAP_CANCEL,
    MANUAL_MAP_APP_RESTART,
} manual_map_out_t;

/*
 * Three standard RGB565 primaries in canonical order; user says which R/G/B they see for each.
 * Builds session->spi_logical_rgb565[L] = word to send so logical primary L appears on glass.
 */
static manual_map_out_t spi_run_manual_colour_mapping_interview(test_session_t *s)
{
    static const uint16_t k_std[3] = { 0xF800u, 0x07E0u, 0x001Fu };
    static const char *const k_std_name[3] = { "red", "green", "blue" };

    printf(
        "\n  Manual colour mapping — three solid screens (standard RGB565 red, green, blue in that send "
        "order).\n"
        "  After each screen finishes drawing: which single primary do you see? r g b (not mixed primaries/white).\n"
        "  Use each of r, g, b exactly once across the three answers (a permutation).\n"
        "  q = cancel and keep the previous mapping.\n");
    for (;;) {
        uint8_t seen_as[3];

        for (int i = 0; i < 3; i++) {
            printf("  [%d/3] Drawing standard %s (0x%04X) — watch the panel…\n", i + 1, k_std_name[i],
                   (unsigned)k_std[i]);
            esp_err_t err = panel_hw_spi_paint_discovery_gram_rgb565(s, k_std[i]);
            if (err != ESP_OK) {
                err = panel_hw_fill_rgb565(k_std[i]);
            }
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "mapping interview fill: %s", esp_err_to_name(err));
                return MANUAL_MAP_CANCEL;
            }
            /* End prompt lines with \\n before blocking read — many UART terminals do not paint a line
             * until newline (prompts ending in ": " appear only after the next line arrives). */
            printf("  [%d/3] The panel should now show standard %s (sent 0x%04X).\n", i + 1, k_std_name[i],
                   (unsigned)k_std[i]);
            printf("  Which primary do you see? Type r, g, or b (q = cancel).\n");
            int c = serial_read_menu_choice("rgbq", s);
            if (c == SERIAL_KEY_APP_RESTART) {
                return MANUAL_MAP_APP_RESTART;
            }
            if (c == SERIAL_KEY_DISPLAY_RECOVERED) {
                i--;
                continue;
            }
            if (c == 'q') {
                return MANUAL_MAP_CANCEL;
            }
            seen_as[i] = (uint8_t)(c == 'r' ? 0 : (c == 'g' ? 1 : 2));
        }

        unsigned cnt[3] = { 0, 0, 0 };
        for (int j = 0; j < 3; j++) {
            cnt[seen_as[j]]++;
        }
        if (cnt[0] != 1u || cnt[1] != 1u || cnt[2] != 1u) {
            printf(
                "  Answers must be a permutation of r, g, b (each exactly once). Try the interview again.\n");
            continue;
        }

        for (int L = 0; L < 3; L++) {
            int j;
            for (j = 0; j < 3; j++) {
                if (seen_as[j] == L) {
                    break;
                }
            }
            s->spi_logical_rgb565[L] = k_std[j];
        }
        printf("  Mapping saved: logical RED=0x%04X GREEN=0x%04X BLUE=0x%04X\n", (unsigned)s->spi_logical_rgb565[0],
               (unsigned)s->spi_logical_rgb565[1], (unsigned)s->spi_logical_rgb565[2]);
        return MANUAL_MAP_DONE;
    }
}

/*
 * Phase 0b: RGB565 primaries + labels, tri-state, optional format loop (plan §3.0, §12).
 * ILI9488: skip demo (no dedicated 18 bpp path); magenta phase still runs.
 */
static spi_phase0b_result_t spi_run_phase0b_tri_state_format_loop(test_session_t *s)
{
    if (panel_hw_bits_per_pixel() != 16) {
        printf("\n  (ILI9488 18 bpp: RED/GREEN/BLUE labelled demo not implemented here — secondary color test follows.)\n");
        session_print_display_truth(s, "Step 1: Color Alignment (18 bpp)");
        return SPI_P0B_OK;
    }

    session_print_display_truth(s, "Step 1: Color Alignment");

    unsigned fmt_changes = 0;

    for (;;) {
phase0b_restart_from_primaries:
        esp_err_t e = panel_hw_spi_run_phase0b_rgb_demo(s);
        if (e != ESP_OK) {
            ESP_LOGE(TAG, "RGB demo failed: %s", esp_err_to_name(e));
            panel_hw_deinit();
            session_reset_display_fields(s);
            return SPI_P0B_FAIL_TRIAL;
        }

        serial_discard_buffered_console_input();
        printf(
            "\n  Each screen uses the mapped logical primary; labels read RED, GREEN, BLUE for that order.\n"
            "  If colours are wrong, choose 2: m = manual mapping interview (r/g/b per standard screen),\n"
            "  i = invert. Then the demo runs again.\n"
            "  Did the panel show solid red, then green, then blue with matching labels?\n"
            "  1  Next — try another chip/size (snow, garbage, unusable)\n"
            "  2  Visible colours or labels do not match — adjust format (then demo repeats)\n"
            "  3  OK — primaries match (then R|G/B|G/R|B secondaries and white, with labels)\n"
            "  r  Repeat the red/green/blue sequence (same settings — e.g. missed it the first time)\n"
            "  Choice — type 1, 2, 3, or r:\n");
        int c = serial_read_menu_choice("123r", s);
        if (c == SERIAL_KEY_APP_RESTART) {
            return SPI_P0B_APP_RESTART;
        }
        if (c == SERIAL_KEY_DISPLAY_RECOVERED) {
            continue;
        }
        if (c == 'r') {
            continue;
        }
        if (c == '3') {
            printf("  Primaries OK. Look at the panel now — check the last primary and its label match.\n");
            printf("  Secondaries (R+G / G+B / R+B / white) start in 1.5 s…\n");
            vTaskDelay(pdMS_TO_TICKS(1500));
            printf("  Running secondaries: four full screens (~1 s each, ~4 s total); watch the panel.\n");
            for (;;) {
                esp_err_t e2 = panel_hw_spi_run_phase0b_secondaries_demo(s);
                if (e2 != ESP_OK) {
                    ESP_LOGE(TAG, "Secondaries demo failed: %s", esp_err_to_name(e2));
                    panel_hw_deinit();
                    session_reset_display_fields(s);
                    return SPI_P0B_FAIL_TRIAL;
                }
                printf("  Secondaries sequence finished.\n");
                serial_discard_buffered_console_input();
                for (;;) {
                    printf(
                        "\n  Secondaries use mapped primaries: R+G=red|green, G+B=green|blue,\n"
                        "  magenta=red|blue, white=all. Labels: YELLOW, CYAN, MAGENTA, WHITE.\n"
                        "  Did those four match the labels?\n"
                        "  1  Next — try another chip/size (unusable)\n"
                        "  2  Wrong — redo from primaries (m / i)\n"
                        "  3  OK — Step 1 complete (color alignment)\n"
                        "  r  Repeat secondaries only\n"
                        "  Choice — type 1, 2, 3, or r:\n");
                    int c2 = serial_read_menu_choice("123r", s);
                    if (c2 == SERIAL_KEY_APP_RESTART) {
                        return SPI_P0B_APP_RESTART;
                    }
                    if (c2 == SERIAL_KEY_DISPLAY_RECOVERED) {
                        continue;
                    }
                    if (c2 == 'r') {
                        break;
                    }
                    if (c2 == '3') {
                        printf("  Step 1 complete — next: Step 2 (arrow toward top on the panel), then size check, then secondary color test.\n");
                        return SPI_P0B_OK;
                    }
                    if (c2 == '1') {
                        printf("  Trying next chip/size pair…\n");
                        return SPI_P0B_FAIL_TRIAL;
                    }
                    if (c2 == '2') {
                        goto phase0b_restart_from_primaries;
                    }
                }
            }
        }
        if (c == '1') {
            printf("  Trying next chip/size pair…\n");
            return SPI_P0B_FAIL_TRIAL;
        }

        for (;;) {
            if (fmt_changes >= 8) {
                printf("\n  Many colour-format tries — 0 = next profile, c = repeat RGB demo.\n"
                       "  Choice — type 0 or c:\n");
                int cap = serial_read_menu_choice("0c", s);
                if (cap == SERIAL_KEY_APP_RESTART) {
                    return SPI_P0B_APP_RESTART;
                }
                if (cap == SERIAL_KEY_DISPLAY_RECOVERED) {
                    continue;
                }
                if (cap == '0') {
                    return SPI_P0B_FAIL_TRIAL;
                }
                break;
            }

            printf(
                "\n  Colour format (then RED/GREEN/BLUE demo runs again):\n"
                "  i  Toggle invert\n"
                "  m  Manual colour mapping — say r/g/b for each standard red/green/blue screen\n"
                "  r  Re-run demo without changing settings\n"
                "  0  Next profile — try another chip/size\n"
                "  Choice — type i, m, r, or 0:\n");
            int f = serial_read_menu_choice("imr0", s);
            if (f == SERIAL_KEY_APP_RESTART) {
                return SPI_P0B_APP_RESTART;
            }
            if (f == SERIAL_KEY_DISPLAY_RECOVERED) {
                continue;
            }
            if (f == '0') {
                return SPI_P0B_FAIL_TRIAL;
            }
            if (f == 'r') {
                break;
            }
            if (f == 'i') {
                s->inv_en = !s->inv_en;
                panel_hw_set_inversion(s);
                fmt_changes++;
                break;
            }
            if (f == 'm') {
                manual_map_out_t mo = spi_run_manual_colour_mapping_interview(s);
                if (mo == MANUAL_MAP_APP_RESTART) {
                    return SPI_P0B_APP_RESTART;
                }
                if (mo == MANUAL_MAP_DONE) {
                    fmt_changes++;
                    break;
                }
                continue;
            }
        }
    }
}

typedef enum {
    SPI_FLOW_POST_P0B_OK = 0,
    SPI_FLOW_APP_RESTART,
    SPI_FLOW_FAIL_TRIAL,
} spi_post_init_flow_t;

/* Shared path after SPI init succeeds: color alignment, orientation, size check, secondary color test fill. */
static spi_post_init_flow_t spi_flow_phase0b_orientation_size_magenta(test_session_t *s)
{
    spi_phase0b_result_t p0 = spi_run_phase0b_tri_state_format_loop(s);
    if (p0 == SPI_P0B_APP_RESTART) {
        return SPI_FLOW_APP_RESTART;
    }
    if (p0 == SPI_P0B_FAIL_TRIAL) {
        return SPI_FLOW_FAIL_TRIAL;
    }
    if (panel_hw_is_spi() && panel_hw_bits_per_pixel() == 16) {
        if (!stage_2_run_orientation(s)) {
            return SPI_FLOW_APP_RESTART;
        }
    }
    if (!stage_3_run_probe(s)) {
        return SPI_FLOW_APP_RESTART;
    }
    if (panel_hw_bits_per_pixel() == 16) {
        (void)panel_hw_spi_paint_discovery_gram_rgb565(s, ui_color_probe_fill(s));
    } else {
        (void)panel_hw_fill_rgb565(ui_color_probe_fill(s));
    }
    return SPI_FLOW_POST_P0B_OK;
}

static const char k_spi_try_intro[] =
    "You will be asked for glass resolution first (often public on the module); the controller IC is often unknown.\n"
    "Then the firmware steps through chip+size pairs (smallest area first). Use 1 / . / Step 1 \"Next\" to move on.\n"
    "Known chip already: M manual. After each successful init: color alignment, on-panel orientation, color-gate "
    "screen alignment, then secondary color test.\n"
    "Format menu: m = map colours, i = invert. Secondary color screen: y = accept, n = next trial, q = stop this pass.\n";

static bool spi_trial_matches_resolution(uint16_t filter_w, uint16_t filter_h, const spi_autotrial_t *t)
{
    if (filter_w == 0 && filter_h == 0) {
        return true;
    }
    if (t->w == filter_w && t->h == filter_h) {
        return true;
    }
    /* Seller WxH may be portrait or landscape vs our trial bookkeeping. */
    if (t->w == filter_h && t->h == filter_w) {
        return true;
    }
    return false;
}

static bool spi_try_prompt_resolution(test_session_t *s, uint16_t *out_w, uint16_t *out_h)
{
    char line[48];
    printf("> ");
    int n = serial_read_line_safe(line, sizeof(line), s);
    if (n == SERIAL_LINE_BOOT_RESTART) {
        return false;
    }
    unsigned a = 0, b = 0;
    if (!parse_uu(line, &a, &b) || a == 0 || b == 0 || a > 480 || b > 800) {
        *out_w = 0;
        *out_h = 0;
        printf("  No filter — will try every chip/size in the list.\n");
        return true;
    }
    *out_w = (uint16_t)a;
    *out_h = (uint16_t)b;
    if (*out_w == *out_h) {
        printf("  Filter: trials for %ux%u only (others skipped).\n", (unsigned)*out_w, (unsigned)*out_h);
    } else {
        printf("  Filter: trials for %ux%u or %ux%u (orientation ignored; others skipped).\n", (unsigned)*out_w,
               (unsigned)*out_h, (unsigned)*out_h, (unsigned)*out_w);
    }
    return true;
}

static bool spi_try_autosequence(test_session_t *s)
{
    printf("\n");
    console_print_wrapped("", k_spi_try_intro);

    const int n = (int)(sizeof(k_spi_trials) / sizeof(k_spi_trials[0]));

    for (;;) {
        printf("\n--- Glass resolution (optional) ---\n");
        printf("Often printed on the seller page or silkscreen; the driver chip may be lasered off.\n");
        printf("Enter alone = no filter; or type W H (e.g. 128 160). Order does not matter (128 160 = 160 128):\n");
        uint16_t filter_w = 0, filter_h = 0;
        if (!spi_try_prompt_resolution(s, &filter_w, &filter_h)) {
            return false;
        }

        bool quit_to_spi_menu = false;
        int ran_matching = 0;

        for (int i = 0; i < n; i++) {
            const spi_autotrial_t *t = &k_spi_trials[i];
            if (!spi_trial_matches_resolution(filter_w, filter_h, t)) {
                if (filter_w != 0 || filter_h != 0) {
                    if (filter_w == filter_h) {
                        printf("  [%d/%d] Skip (not %ux%u): %s\n", i + 1, n, (unsigned)filter_w, (unsigned)filter_h,
                               t->hint);
                    } else {
                        printf("  [%d/%d] Skip (not %ux%u / %ux%u): %s\n", i + 1, n, (unsigned)filter_w,
                               (unsigned)filter_h, (unsigned)filter_h, (unsigned)filter_w, t->hint);
                    }
                }
                continue;
            }
            ran_matching++;

        retry_trial:
            panel_hw_deinit();
            session_reset_display_fields(s);

            esp_err_t err = panel_hw_spi_init(s, t->chip, t->w, t->h, t->pclk_hz);
            if (err != ESP_OK) {
                printf("  [%d/%d] Skip (init failed): %s — %s\n", i + 1, n, esp_err_to_name(err), t->hint);
                continue;
            }

            session_print_display_truth(s, "SPI try sequence (after init)");

            spi_post_init_flow_t flow = spi_flow_phase0b_orientation_size_magenta(s);
            if (flow == SPI_FLOW_APP_RESTART) {
                return false;
            }
            if (flow == SPI_FLOW_FAIL_TRIAL) {
                printf("  [%d/%d] Moving to next chip/size…\n", i + 1, n);
                if (panel_hw_panel_ready()) {
                    panel_hw_deinit();
                }
                session_reset_display_fields(s);
                continue;
            }

            printf("  [%d/%d] %s\n", i + 1, n, t->hint);
            print_spi_chip_bench_tips(t->chip, "      ");
            if (t->trial_extra) {
                console_print_wrapped("      ", t->trial_extra);
            }
            printf("      Stable solid screen for secondary color test (magenta mix, not snow/noise)? y / n / q (Enter = y):\n");
            int c = serial_read_menu_choice_ynq(s);
            if (c == SERIAL_KEY_APP_RESTART) {
                return false;
            }
            if (c == SERIAL_KEY_DISPLAY_RECOVERED) {
                goto retry_trial;
            }
            if (c == 'y') {
                console_print_wrapped(
                    "",
                    "Using this profile. Tune orientation and gap in later steps; brand graphic appears after test patterns.\n");
                return true;
            }
            if (c == 'q') {
                printf("Stopped this pass — returning to SPI menu.\n");
                quit_to_spi_menu = true;
                break;
            }
        }

        if (quit_to_spi_menu) {
            break;
        }

        if (ran_matching == 0 && (filter_w != 0 || filter_h != 0)) {
            if (filter_w == filter_h) {
                printf("No trial in the list matches %ux%u — press Enter next time for all sizes, or use M manual.\n",
                       (unsigned)filter_w, (unsigned)filter_h);
            } else {
                printf(
                    "No trial in the list matches %ux%u (or %ux%u) — press Enter next time for all sizes, or use M manual.\n",
                    (unsigned)filter_w, (unsigned)filter_h, (unsigned)filter_h, (unsigned)filter_w);
            }
        }

        printf("\nEnd of this pass through matching trials.\n");
        printf("c = cycle: run again from the first trial (you can change resolution),\n");
        printf("q = quit to SPI menu:\n");
        int cyc = serial_read_menu_choice("cq", s);
        if (cyc == SERIAL_KEY_APP_RESTART) {
            return false;
        }
        if (cyc == 'q') {
            break;
        }
    }

    panel_hw_deinit();
    session_reset_display_fields(s);
    printf("Try sequence done — T again or M manual.\n");
    return true;
}

/* Manual chip pick (M): lines 1–6 match k_spi_chips[] order in spi_presets.c (Phase 8.15: by typical resolution). */
static void spi_manual_print_chip_menu(void)
{
    printf("\nManual — match the marking on the flex or PCB:\n");
    printf("(Grouped by typical active area / resolution class.)\n\n");
    printf("--- Small / bar (typ. ~128–162 px row, or 135×240 bar) ---\n");
    printf("  1  ST7735   (often printed ST7735 / HSGT7735)\n");
    printf("  2  ST7789   (ST7789V / 7789 — bar & square IPS common)\n\n");
    printf("--- ~240×240 (round) ---\n");
    printf("  3  GC9A01   (GC9A01 / 9A01, round modules)\n\n");
    printf("--- ~240×320 (2.4\" class) ---\n");
    printf("  4  ILI9341  (ILI9341 / ILI9342 / 9341)\n\n");
    printf("--- ~320×480 (3.5\" class) ---\n");
    printf("  5  ILI9488  (ILI9488)\n");
    printf("  6  ST7796   (ST7796 SPI — esp_lcd factory pending; presets for geometry only)\n\n");
    printf("  Q  Back to M/T menu\n  Choice — type 1–6 or q:\n");
}

static void print_geometry_preset_lines(const spi_chip_desc_t *chip)
{
    printf("Geometry preset (or C custom WxH):\n");
    printf("  1 %s", chip->presets[0].label);
    for (size_t j = 1; j < chip->n_presets; j++) {
        printf("   %zu %s", j + 1, chip->presets[j].label);
    }
    printf("\n");
}

static bool spi_manual_chip_geometry(test_session_t *s)
{
    const size_t n_chips = k_spi_n_spi_chips;

    for (;;) {
        spi_manual_print_chip_menu();
        int c = serial_read_menu_choice("123456q", s);
        if (c == SERIAL_KEY_APP_RESTART) {
            return false;
        }
        if (c == SERIAL_KEY_DISPLAY_RECOVERED) {
            continue;
        }
        if (c == 'q') {
            return true;
        }
        if (c < '1' || c > '6') {
            continue;
        }
        size_t chip_idx = (size_t)(c - '1');
        if (chip_idx >= n_chips) {
            continue;
        }
        const spi_chip_desc_t *chip = &k_spi_chips[chip_idx];

        printf("Tips for this controller:\n");
        print_spi_chip_bench_tips(chip->chip, "  ");
        print_geometry_preset_lines(chip);

        char valid_geo[16];
        if (chip->n_presets + 2 > sizeof(valid_geo)) {
            printf("Internal error: too many presets for this chip.\n");
            continue;
        }
        for (size_t j = 0; j < chip->n_presets; j++) {
            valid_geo[j] = (char)('1' + j);
        }
        valid_geo[chip->n_presets] = 'c';
        valid_geo[chip->n_presets + 1] = '\0';

        printf("  Choice — preset number or c:\n");
        int g = serial_read_menu_choice(valid_geo, s);
        if (g == SERIAL_KEY_APP_RESTART) {
            return false;
        }
        if (g == SERIAL_KEY_DISPLAY_RECOVERED) {
            continue;
        }

        uint16_t w = 0, h = 0;
        if (g == 'c') {
            printf("Enter width height (e.g. 240 320): ");
            char line[40];
            int n = serial_read_line_safe(line, sizeof(line), s);
            if (n == SERIAL_LINE_BOOT_RESTART) {
                return false;
            }
            unsigned a, b;
            if (!parse_uu(line, &a, &b) || a == 0 || b == 0 || a > 480 || b > 800) {
                printf("Invalid.\n");
                continue;
            }
            w = (uint16_t)a;
            h = (uint16_t)b;
        } else {
            int idx = g - '1';
            if (idx < 0 || (size_t)idx >= chip->n_presets) {
                printf("Unknown preset.\n");
                continue;
            }
            w = chip->presets[idx].w;
            h = chip->presets[idx].h;
        }

        if (w == 0 || h == 0) {
            printf("Select a valid preset.\n");
            continue;
        }

        uint32_t pclk = chip->default_pclk_hz;
        esp_err_t err = panel_hw_spi_init(s, chip->chip, w, h, pclk);
        if (err == ESP_OK) {
            session_print_display_truth(s, "SPI manual (after init)");
            spi_post_init_flow_t flow = spi_flow_phase0b_orientation_size_magenta(s);
            if (flow == SPI_FLOW_APP_RESTART) {
                return false;
            }
            if (flow == SPI_FLOW_FAIL_TRIAL) {
                if (panel_hw_panel_ready()) {
                    panel_hw_deinit();
                }
                session_reset_display_fields(s);
                printf("Profile not confirmed — pick another chipset or geometry.\n");
                continue;
            }
            printf("Stable solid screen for secondary color test (magenta mix, not snow)? y or n (Enter = y):\n");
            int yn = serial_read_menu_choice_yesno(s);
            if (yn == SERIAL_KEY_APP_RESTART) {
                return false;
            }
            if (yn == SERIAL_KEY_DISPLAY_RECOVERED) {
                continue;
            }
            if (yn != 'y') {
                panel_hw_deinit();
                session_reset_display_fields(s);
                printf("Try another chipset or geometry.\n");
                continue;
            }
            printf("SPI panel OK — \"%s\" @ %" PRIu32 " Hz PCLK\n", s->profile_tag, pclk);
            console_print_wrapped(
                "", "Tune orientation and gap in the next steps; brand graphic follows test patterns.\n");
            return true;
        }
        printf("Init failed (%s). Try another chipset or geometry.\n", esp_err_to_name(err));
    }
}

bool display_stage_g3(test_session_t *s)
{
    console_clear_screen();
    session_print_display_truth(s, "Panel setup");
    if (s->bus == SESSION_BUS_I2C) {
        if (!i2c_resolve_addr(s)) {
            return false;
        }
        for (;;) {
            printf("\n--- CONTROLS ---\n");
            printf("%-15s : SSD1306 (128x64)\n", "1");
            printf("%-15s : SH1106 (128x64, gap col +2 default)\n", "2");
            printf("%-15s : skip (no init)\n", "Q");
            printf("\n--- NAV ---\n");
            printf("Choice — type 1, 2, or q:\n");
            int c = serial_read_menu_choice("12q", s);
            if (c == SERIAL_KEY_APP_RESTART) {
                return false;
            }
            if (c == SERIAL_KEY_DISPLAY_RECOVERED) {
                continue;
            }
            if (c == 'q') {
                printf("Skipped.\n");
                return true;
            }
            s->hor_res = 128;
            s->ver_res = 64;
            s->ssd1306_height = 64;
            s->gap_row = 0;
            if (c == '1') {
                s->gap_col = 0;
            } else {
                s->gap_col = 2;
            }
            session_i2c_driver_t drv =
                (c == '1') ? SESSION_I2C_DRV_SSD1306 : SESSION_I2C_DRV_SH1106;
            esp_err_t err = panel_hw_i2c_init(s, drv, s->i2c_addr_7bit, 64);
            if (err == ESP_OK) {
                (void)panel_hw_fill_mono(0x00);
                (void)panel_hw_draw_probe_marker_corner();
                printf("I2C panel OK — profile \"%s\"\n", s->profile_tag);
                session_print_display_truth(s, "I2C panel ready");
            } else {
                ESP_LOGE(TAG, "I2C panel init: %s", esp_err_to_name(err));
                printf("Init failed — check wiring and driver choice.\n");
            }
            return true;
        }
    }

    for (;;) {
        printf("\n--- CONTROLS ---\n");
        printf("%-15s : %s\n", "M", "manual — read chip marking on flex/PCB");
        printf("%-15s : %s\n", "T", "try sequence — WxH filter, trials, color alignment, size check, secondary color y/n/q");
        printf("%-15s : %s\n", "Q", "skip SPI init");
        printf("\n--- NAV ---\n");
        printf("Choice — type m, t, or q:\n");
        int top = serial_read_menu_choice("mtq", s);
        if (top == SERIAL_KEY_APP_RESTART) {
            return false;
        }
        if (top == SERIAL_KEY_DISPLAY_RECOVERED) {
            continue;
        }
        if (top == 'q') {
            printf("Skipped SPI init.\n");
            return true;
        }
        if (top == 't') {
            if (!spi_try_autosequence(s)) {
                return false;
            }
            if (s->panel_ready) {
                return true;
            }
            continue;
        }
        if (top == 'm') {
            if (!spi_manual_chip_geometry(s)) {
                return false;
            }
            if (s->panel_ready) {
                return true;
            }
            continue;
        }
    }
}
