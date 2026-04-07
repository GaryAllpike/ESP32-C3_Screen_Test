/* Boot: hookup → safe idle → Enter → identity → overview → guided UI. */
#include "appshell.h"
#include "display_recovery.h"
#include "guided_flow.h"
#include "hookup_print.h"
#include "identity.h"
#include "safe_idle.h"
#include "serial_menu.h"
#include "session.h"
#include "esp_log.h"
#include "esp_err.h"
#include <stdio.h>

static const char *TAG = "appshell";

void appshell_run(void)
{
    for (;;) {
        test_session_t session;
        session_init(&session);
        display_recovery_invalidate();

        hookup_print_instructions();
        esp_err_t err = safe_idle_configure_display_pins();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "safe_idle_configure_display_pins failed: %s", esp_err_to_name(err));
            printf("FATAL: could not configure safe idle GPIO — check board_pins.h / wiring.\n");
            return;
        }

        printf("\n\n------------------------------------------------------------------\n");
        printf("Press Enter when wiring is complete.  (! = full restart  @ = display recover)\n");
        serial_wait_enter_result_t w = serial_wait_enter_hooks(NULL, &session);
        if (w == SERIAL_WAIT_ENTER_BOOT_RESTART) {
            continue;
        }

        err = identity_probe_transport(&session);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "identity_probe_transport: %s", esp_err_to_name(err));
            printf("FATAL: I2C bus init failed.\n");
            return;
        }

        if (!guided_show_overview_and_wait(&session)) {
            continue;
        }

        guided_flow_run(&session);
    }
}
