#pragma once

#include "driver/i2c_master.h"
#include "esp_err.h"
#include "session.h"

void display_i2c_bus_config(i2c_master_bus_config_t *cfg);

/*
 * §3.1 after Enter: I2C init, fast probe, optional full scan.
 * Sets session bus to I2C (first ACK) or SPI if empty / inconclusive for v1 scaffold.
 * Controller identification / init trials — extended in later tasks.
 */
esp_err_t identity_probe_transport(test_session_t *session);
