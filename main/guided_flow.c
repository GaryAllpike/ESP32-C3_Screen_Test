/* Version: 0.9 (Pre-Release) | JBG Discovery Framework */
/*
 * Phase 12.1: full logical black between silicon “marker” and “extent” substeps is done inside
 * stage_3_run_probe() (panel setup / SPI path), not in this loop — those substeps run before marker probe returns.
 */
#include "guided_flow.h"
#include "hookup_print.h"
#include "console_dashboard.h"
#include "console_text.h"
#include "display_recovery.h"
#include "display_stages.h"
#include "guided_ui_strings.h"
#include "provision_print.h"
#include "identity.h"
#include "panel_hw.h"
#include "serial_menu.h"
#include "esp_err.h"
#include "esp_log.h"
#include <stdio.h>

/* Shell: A=Advanced; Enter or . = next; , = back. M/T reserved inside panel setup. G=where am I. */
#define KEYS_GUIDED_MAIN "\n" ".,rago"
#define KEYS_EXPERT_MENU "\n" "12345"

static const char *TAG = "guided";

typedef enum {
    UI_MODE_GUIDED = 0,
    UI_MODE_EXPERT,
} ui_mode_t;

typedef enum {
    STAGE_WIRING_CHECK = 1,
    STAGE_SILICON_ID = 2,
    STAGE_MARKER_PROBE = 3,
    STAGE_SILICON_EXTENTS = 4,
    STAGE_COMPASS_VERIFY = 5,
    STAGE_COLOR_VALIDATION = 6,
    STAGE_PROVISION_CHECK = 7,
    STAGE_FINAL_VERIFY = 8,
    STAGE_RESTART_GATE = 9,
    STAGE_G10 = 10,
    STAGE_COUNT = 11,
} guided_stage_t;

typedef bool (*stage_run_fn)(test_session_t *s);
static const stage_run_fn k_stage_run[STAGE_COUNT] = {
    [STAGE_WIRING_CHECK] = NULL,
    [STAGE_SILICON_ID] = NULL,
    [STAGE_MARKER_PROBE] = display_stage_g3,
    [STAGE_SILICON_EXTENTS] = NULL,
    [STAGE_COMPASS_VERIFY] = display_stage_g5,
    [STAGE_COLOR_VALIDATION] = display_stage_g6,
    [STAGE_PROVISION_CHECK] = NULL,
    [STAGE_FINAL_VERIFY] = display_stage_g8,
    [STAGE_RESTART_GATE] = NULL,
    [STAGE_G10] = display_stage_g10,
};

static void print_stage_banner(guided_stage_t g, const test_session_t *s)
{
    if (g <= STAGE_SILICON_ID) {
        return;
    }
    const guided_stage_meta_t *m = guided_stage_meta((unsigned)g);
    printf("\n\n--- %s ---\n", m->title);
    if (g >= STAGE_MARKER_PROBE) {
        if (s->bus == SESSION_BUS_I2C) {
            printf("Session: I2C 0x%02X\n", s->i2c_addr_7bit);
        } else {
            printf("Session: SPI\n");
        }
    }
    printf("\n");
}

/* Shown when entering panel setup (identity summary only — keys are the panel setup menu). */
static void print_transport_line(const test_session_t *s)
{
    if (s->bus == SESSION_BUS_I2C) {
        printf("Detected I2C at 0x%02X — in panel setup press 1 (SSD1306) or 2 (SH1106).\n\n",
               s->i2c_addr_7bit);
    } else {
        printf("Using SPI — pick M (manual marking) or T (try profiles) in the menu.\n\n");
    }
}

static void guided_abort_for_restart(test_session_t *session)
{
    panel_hw_deinit();
    session_reset_display_fields(session);
}

/*
 * Returns false if the user requested full app restart (!), so caller should restart outer loop.
 */
static bool expert_menu(test_session_t *session, ui_mode_t *mode, guided_stage_t *stage)
{
    for (;;) {
        printf("\n\n--- Advanced menu ---\n\n");
        printf("Change how SPI vs I2C is chosen, reprint settings, resume main steps.\n");
        printf("  1  Return to main steps (resume: %s)\n", guided_stage_meta((unsigned)*stage)->title);
        printf("  2  Print config / provision (same as O in main steps)\n");
        printf("  3  Force SPI on next identity probe (session)\n");
        printf("  4  Force I2C on next identity probe (session)\n");
        printf("  5  Clear transport override (automatic probe)\n");
        printf("Choice (Enter = option 1): ");

        int c = serial_read_menu_choice(KEYS_EXPERT_MENU, session);
        if (c == SERIAL_KEY_APP_RESTART) {
            return false;
        }
        if (c == SERIAL_KEY_DISPLAY_RECOVERED) {
            continue;
        }
        if (c == SERIAL_KEY_ENTER) {
            c = '1';
        }
        if (c == '1') {
            *mode = UI_MODE_GUIDED;
            return true;
        }
        if (c == '2') {
            provision_print_menu(session);
            continue;
        }
        if (c == '3') {
            panel_hw_deinit();
            session_reset_display_fields(session);
            session->transport_override = SESSION_TRANSPORT_FORCE_SPI;
            if (identity_probe_transport(session) != ESP_OK) {
                ESP_LOGE(TAG, "identity re-probe failed");
            }
            continue;
        }
        if (c == '4') {
            panel_hw_deinit();
            session_reset_display_fields(session);
            session->transport_override = SESSION_TRANSPORT_FORCE_I2C;
            if (identity_probe_transport(session) != ESP_OK) {
                ESP_LOGE(TAG, "identity re-probe failed");
            }
            continue;
        }
        if (c == '5') {
            panel_hw_deinit();
            session_reset_display_fields(session);
            session->transport_override = SESSION_TRANSPORT_AUTO;
            if (identity_probe_transport(session) != ESP_OK) {
                ESP_LOGE(TAG, "identity re-probe failed");
            }
            continue;
        }
    }
}

/*
 * Returns false if user pressed ! (full restart).
 */
static bool stage_g1_revisit(test_session_t *session, guided_stage_t *stage)
{
    hookup_print_wiring_pinout_only();
    if (serial_wait_enter_hooks(NULL, session) == SERIAL_WAIT_ENTER_BOOT_RESTART) {
        return false;
    }
    if (identity_probe_transport(session) != ESP_OK) {
        ESP_LOGE(TAG, "identity re-probe after wiring review failed");
        printf("ERROR: display detection failed — check wiring.\n");
    }
    *stage = STAGE_MARKER_PROBE;
    return true;
}

static bool stage_try_advance(guided_stage_t *stage)
{
    if (*stage >= STAGE_RESTART_GATE) {
        return false;
    }
    (*stage)++;
    return true;
}

static bool stage_go_back(guided_stage_t *stage)
{
    if (*stage <= STAGE_WIRING_CHECK) {
        return false;
    }
    (*stage)--;
    return true;
}

bool guided_show_overview_and_wait(test_session_t *session)
{
    for (;;) {
        guided_print_post_identity_overview();
        serial_overview_wait_result_t w =
            serial_wait_continue_or_advanced("\nEnter = continue  A = Advanced  (!/@): ", session);
        if (w == SERIAL_OVERVIEW_BOOT_RESTART) {
            return false;
        }
        if (w == SERIAL_OVERVIEW_CONTINUE) {
            return true;
        }
        ui_mode_t mode = UI_MODE_EXPERT;
        guided_stage_t st = STAGE_MARKER_PROBE;
        if (!expert_menu(session, &mode, &st)) {
            return false;
        }
        /* Option 1 (return to main steps): go straight to guided / panel setup — no second overview. */
        return true;
    }
}

void guided_flow_run(test_session_t *session)
{
    dashboard_init();
    dashboard_refresh_header(session);
    dashboard_refresh_footer(3, session);

    ui_mode_t mode = UI_MODE_GUIDED;
    /* Overview Enter implies next step — first actionable stage is panel setup. */
    guided_stage_t stage = STAGE_MARKER_PROBE;
    guided_stage_t prev_stage = STAGE_WIRING_CHECK;
    /* Full key-help + cursor save on stage/expert/@ and after any display_stage_* runner (they usually
     * clear the screen). Partial STATE + Command lines only when the stage has no runner (e.g. restart gate). */
    bool needs_full_redraw = false;

    for (;;) {
        if (mode == UI_MODE_EXPERT) {
            if (!expert_menu(session, &mode, &stage)) {
                guided_abort_for_restart(session);
                return;
            }
            if (session->start_g10_after_provision) {
                stage = STAGE_G10;
                session->start_g10_after_provision = false;
                prev_stage = STAGE_WIRING_CHECK;
            }
            needs_full_redraw = true;
            continue;
        }

        if (stage != prev_stage) {
            if (session->panel_ready && panel_hw_panel_ready()) {
                panel_hw_nuclear_clear(session);
            }
            print_stage_banner(stage, session);
            if (stage == STAGE_MARKER_PROBE) {
                print_transport_line(session);
            }
            if (stage == STAGE_RESTART_GATE) {
                printf("\nEnd of main steps — profile on serial; brand on panel.\n\n");
                printf("Main path complete. . or Enter returns to panel setup; , from there revisits wiring.\n");
                printf("R restarts from wiring review; A opens Advanced menu.\n\n");
            }
            prev_stage = stage;
            needs_full_redraw = true;
        }

        if (stage >= STAGE_MARKER_PROBE && stage <= STAGE_G10) {
            bool ok;
            /* Silicon extents = orientation; provision check = verification; marker probe = panel setup. */
            switch (stage) {
            case STAGE_SILICON_EXTENTS:
                ok = stage_2_run_orientation(session);
                break;
            case STAGE_PROVISION_CHECK:
                ok = stage_4_run_verification(session);
                break;
            default: {
                stage_run_fn fn = k_stage_run[stage];
                ok = (fn == NULL) || fn(session);
                break;
            }
            }
            if (!ok) {
                guided_abort_for_restart(session);
                return;
            }
            if (k_stage_run[stage] != NULL || stage == STAGE_SILICON_EXTENTS || stage == STAGE_PROVISION_CHECK ||
                stage == STAGE_RESTART_GATE) {
                needs_full_redraw = true;
            }
            if (stage == STAGE_MARKER_PROBE && session->panel_ready) {
                display_recovery_snapshot(session);
            }
        }

        if (session->guided_override_next_stage != 0) {
            uint8_t t = session->guided_override_next_stage;
            session->guided_override_next_stage = 0;
            if (t == 5) {
                stage = STAGE_COMPASS_VERIFY;
            } else if (t == 7) {
                stage = STAGE_PROVISION_CHECK;
            }
            prev_stage = STAGE_WIRING_CHECK;
            continue;
        }

        if (needs_full_redraw) {
            dashboard_refresh_header(session);
            dashboard_refresh_footer((unsigned)stage, session);
            dashboard_body_home();
            guided_print_shell_key_help((unsigned)stage);
            dashboard_show_prompt();
            console_cursor_save();
            needs_full_redraw = false;
        } else {
            console_cursor_restore_clear_below();
            printf("\n--- STATE ---\n");
            printf("%-15s : %s\n", "Step", guided_stage_meta((unsigned)stage)->title);
            printf("\n--- NAV ---\n");
            dashboard_show_prompt();
        }

        int raw = serial_read_menu_choice(KEYS_GUIDED_MAIN, session);
        if (raw == SERIAL_KEY_APP_RESTART) {
            guided_abort_for_restart(session);
            return;
        }
        if (raw == SERIAL_KEY_DISPLAY_RECOVERED) {
            needs_full_redraw = true;
            continue;
        }
        int c = raw;

        switch (c) {
        case 'a':
            mode = UI_MODE_EXPERT;
            continue;
        case 'g': {
            const guided_stage_meta_t *m = guided_stage_meta((unsigned)stage);
            printf("\nYou are at: %s — %s\n", m->title, m->blurb);
            continue;
        }
        case 'r':
            panel_hw_deinit();
            session_reset_display_fields(session);
            stage = STAGE_WIRING_CHECK;
            if (!stage_g1_revisit(session, &stage)) {
                guided_abort_for_restart(session);
                return;
            }
            prev_stage = STAGE_WIRING_CHECK;
            continue;
        case ',':
            if (stage == STAGE_MARKER_PROBE) {
                if (!stage_g1_revisit(session, &stage)) {
                    guided_abort_for_restart(session);
                    return;
                }
                prev_stage = STAGE_WIRING_CHECK;
            } else if (!stage_go_back(&stage)) {
                printf("Already at first step.\n");
            }
            continue;
        case '.':
        case SERIAL_KEY_ENTER:
            if (stage == STAGE_RESTART_GATE) {
                stage = STAGE_MARKER_PROBE;
                prev_stage = STAGE_WIRING_CHECK;
                printf("Returning to panel setup.\n");
                continue;
            }
            if (session->batch_dot_opens_provision &&
                (stage == STAGE_PROVISION_CHECK || stage == STAGE_FINAL_VERIFY)) {
                provision_print_menu(session);
                if (session->start_g10_after_provision) {
                    stage = STAGE_G10;
                    session->start_g10_after_provision = false;
                } else if (!stage_try_advance(&stage)) {
                    printf("At last step.\n");
                }
                prev_stage = STAGE_WIRING_CHECK;
                continue;
            }
            if (!stage_try_advance(&stage)) {
                printf("At last step.\n");
            }
            continue;
        case 'o':
            provision_print_menu(session);
            if (session->start_g10_after_provision) {
                stage = STAGE_G10;
                session->start_g10_after_provision = false;
                prev_stage = STAGE_WIRING_CHECK;
            }
            continue;
        default:
            continue;
        }
    }
}
