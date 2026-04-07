#include "display_recovery.h"
#include "panel_hw.h"
#include "ui_colors.h"
#include "esp_err.h"
#include <stdio.h>
#include <string.h>

static test_session_t s_snap;
static bool s_snap_ok;

void display_recovery_invalidate(void)
{
    s_snap_ok = false;
    memset(&s_snap, 0, sizeof(s_snap));
}

void display_recovery_snapshot(const test_session_t *s)
{
    if (!s->panel_ready) {
        return;
    }
    memcpy(&s_snap, s, sizeof(*s));
    s_snap_ok = true;
}

bool display_recovery_has_snapshot(void)
{
    return s_snap_ok;
}

void display_recovery_restore(test_session_t *s)
{
    if (!s_snap_ok) {
        printf("(No snapshot yet — use ! to restart from hookup after Enter.)\n");
        return;
    }

    session_transport_override_t tr = s->transport_override;

    panel_hw_deinit();
    memcpy(s, &s_snap, sizeof(*s));
    /* Snapshot supplies bus, I2C addr/ack, and display fields. Keep only the live
       transport_override — it is user preference from the expert menu, not part
       of the captured display state. */
    s->transport_override = tr;

    if (!s->panel_ready) {
        printf("(Snapshot had no active panel.)\n");
        return;
    }

    esp_err_t err = ESP_FAIL;
    if (s->bus == SESSION_BUS_SPI && s->spi_chip != SESSION_SPI_CHIP_NONE) {
        uint32_t pc = s->spi_pclk_hz;
        if (pc == 0 || pc > 40 * 1000 * 1000) {
            pc = 20 * 1000 * 1000;
        }
        err = panel_hw_spi_init(s, s->spi_chip, s->hor_res, s->ver_res, pc);
    } else if (s->bus == SESSION_BUS_I2C && s->i2c_driver != SESSION_I2C_DRV_NONE) {
        err = panel_hw_i2c_init(s, s->i2c_driver, s->i2c_addr_7bit, s->ssd1306_height);
    } else {
        printf("(Snapshot bus/profile mismatch — try !)\n");
        return;
    }

    if (err != ESP_OK) {
        printf("Recovery re-init failed (%s) — try ! to restart from hookup.\n", esp_err_to_name(err));
        session_reset_display_fields(s);
        return;
    }

    panel_hw_apply_gap(s);
    panel_hw_apply_orientation(s);
    panel_hw_apply_invert(s);
    panel_hw_set_backlight_pct(s);
    if (panel_hw_is_spi()) {
        (void)panel_hw_fill_rgb565(ui_color_bg(s));
    } else if (panel_hw_is_i2c()) {
        (void)panel_hw_fill_mono(0x00);
    }
    printf("[Display restored from last snapshot @ <=40 MHz SPI if applicable.]\n");
}
