#pragma once

#include "esp_err.h"

/* §6.1.2 — SPI lines safe idle before aggressive bus activity (no SPI2 init yet). */
esp_err_t safe_idle_configure_display_pins(void);
