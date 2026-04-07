/* Version: 0.9 (Pre-Release) | JBG Discovery Framework */
#include "console_dashboard.h"
#include "guided_ui_strings.h"
#include "panel_hw.h"
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

static int dashboard_scroll_top(void)
{
    return 4;
}

static int dashboard_scroll_bottom(void)
{
    return DASHBOARD_TERM_ROWS - 3;
}

static void dashboard_border_hline(int row, bool top_corner, bool is_footer_divider)
{
    printf("\033[%d;1H", row);
    if (top_corner) {
        fputs("╔", stdout);
    } else if (is_footer_divider) {
        fputs("╠", stdout);
    } else {
        fputs("╚", stdout);
    }

    for (int i = 0; i < DASHBOARD_TERM_COLS - 2; i++) {
        fputs("═", stdout);
    }

    if (top_corner) {
        fputs("╗", stdout);
    } else if (is_footer_divider) {
        fputs("╣", stdout);
    } else {
        fputs("╝", stdout);
    }
}

void dashboard_init(void)
{
    /* Reset margins, clear screen, then middle-band scroll region (rows 4 .. H-3). */
    fputs("\033[r\033[2J\033[1;1H", stdout);
    const int bot = dashboard_scroll_bottom();
    printf("\033[%d;%dr", dashboard_scroll_top(), bot);
    fputs("\033[1;1H", stdout);
}

void dashboard_body_home(void)
{
    printf("\033[%d;1H", dashboard_scroll_top());
}

void dashboard_refresh_header(const test_session_t *s)
{
    if (!s) {
        return;
    }
    const char *model = session_model_label(s);
    char line[192];
    snprintf(line, sizeof line,
             "JBG v0.9 | %-8s | %ux%u | gap %+d,%+d | inv %d | MADCTL 0x%02X | phys %ux%u", model,
             (unsigned)s->hor_res, (unsigned)s->ver_res, (int)s->gap_col, (int)s->gap_row, s->inv_en ? 1 : 0,
             (unsigned)s->madctl, (unsigned)s->phys_w, (unsigned)s->phys_h);
    if (s->bus == SESSION_BUS_I2C && s->i2c_addr_7bit >= 0x08 && s->i2c_addr_7bit <= 0x77) {
        size_t n = strnlen(line, sizeof line);
        snprintf(line + n, sizeof line - n, " | I2C 0x%02X", (unsigned)s->i2c_addr_7bit);
    } else if (s->bus == SESSION_BUS_SPI && s->spi_pclk_hz > 0) {
        size_t n = strnlen(line, sizeof line);
        snprintf(line + n, sizeof line - n, " | PCLK %" PRIu32, s->spi_pclk_hz);
    }

    dashboard_border_hline(1, true, false);

    const int inner = DASHBOARD_TERM_COLS - 4;
    printf("\033[2;1H║ %-*.*s ║", inner, inner, line);

    dashboard_border_hline(3, false, false);
}

void dashboard_refresh_footer(unsigned stage_1_to_10, const test_session_t *s)
{
    (void)s;
    if (stage_1_to_10 == 0 || stage_1_to_10 > 10) {
        stage_1_to_10 = 3;
    }
    const guided_stage_meta_t *m = guided_stage_meta(stage_1_to_10);

    dashboard_border_hline(22, false, true);

    const char *hint = ". Enter: next  ,: prev  R A O G  ! @";
    if (stage_1_to_10 >= 9) {
        hint = ". Enter: panel setup  ,: prev  R A O G  ! @";
    } else if (stage_1_to_10 == 3) {
        hint = ". Enter: next  ,: wiring  R A O G  ! @";
    }
    printf("\033[23;1H\033[2KStep: %s  %s", m->title, hint);

    dashboard_border_hline(DASHBOARD_TERM_ROWS, false, false);
}

void dashboard_show_prompt(void)
{
    printf("\033[%d;1H\033[2KCommand: ", DASHBOARD_TERM_ROWS);
}
