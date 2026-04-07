#include "identity.h"
#include "board_pins.h"
#include "driver/i2c_master.h"
#include "esp_check.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "identity";

void display_i2c_bus_config(i2c_master_bus_config_t *cfg)
{
    /* Full zero-init: on targets without SOC_I2C_SUPPORT_SLEEP_RETENTION, allow_pd must stay 0
     * or i2c_new_master_bus returns ESP_ERR_NOT_SUPPORTED ("not able to power down in light sleep"). */
    memset(cfg, 0, sizeof(*cfg));
    cfg->i2c_port = I2C_NUM_0;
    cfg->sda_io_num = BOARD_DISPLAY_I2C_SDA;
    cfg->scl_io_num = BOARD_DISPLAY_I2C_SCL;
    cfg->clk_source = I2C_CLK_SRC_DEFAULT;
    cfg->glitch_ignore_cnt = 7;
    cfg->flags.enable_internal_pullup = true;
}

/* §3.3 — fast list first */
static const uint8_t k_fast_addrs[] = {0x3C, 0x3D};

static esp_err_t probe_addr(i2c_master_bus_handle_t bus, uint8_t addr_7bit, bool *ack)
{
    esp_err_t e = i2c_master_probe(bus, addr_7bit, 80);
    if (e == ESP_OK) {
        *ack = true;
        return ESP_OK;
    }
    *ack = false;
    /* ESP_ERR_NOT_FOUND is normal (address absent). Anything else is a bus fault. */
    return (e == ESP_ERR_NOT_FOUND) ? ESP_OK : e;
}

esp_err_t identity_probe_transport(test_session_t *session)
{
    if (session->transport_override == SESSION_TRANSPORT_FORCE_SPI) {
        session->bus = SESSION_BUS_SPI;
        session->i2c_addr_7bit = 0;
        session->i2c_ack_seen = false;
        printf("[identity] Forced SPI (session override).\n");
        return ESP_OK;
    }

    const bool force_i2c = (session->transport_override == SESSION_TRANSPORT_FORCE_I2C);
    session->i2c_ack_seen = false;

    i2c_master_bus_config_t bus_cfg;
    display_i2c_bus_config(&bus_cfg);

    i2c_master_bus_handle_t bus = NULL;
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_cfg, &bus), TAG, "i2c_new_master_bus");

    bool found = false;
    uint8_t found_addr = 0;

    for (size_t i = 0; i < sizeof(k_fast_addrs); i++) {
        bool ack = false;
        esp_err_t pe = probe_addr(bus, k_fast_addrs[i], &ack);
        if (pe != ESP_OK) {
            printf("[identity] I2C bus fault during probe (not just missing device) — check SDA/SCL.\n");
            ESP_LOGE(TAG, "I2C probe bus fault at 0x%02X: %s", k_fast_addrs[i], esp_err_to_name(pe));
            i2c_del_master_bus(bus);
            return pe;
        }
        if (ack) {
            found = true;
            found_addr = k_fast_addrs[i];
            session->i2c_ack_seen = true;
            ESP_LOGI(TAG, "I2C ACK at 0x%02X (fast probe)", found_addr);
            break;
        }
    }

    if (!found) {
        ESP_LOGI(TAG, "Fast probe empty — full scan 0x08..0x77 (SPEC §3.3)");
        for (uint16_t a = 0x08; a <= 0x77; a++) {
            bool ack = false;
            esp_err_t pe = probe_addr(bus, (uint8_t)a, &ack);
            if (pe != ESP_OK) {
                printf("[identity] I2C bus fault during full scan — check SDA/SCL.\n");
                ESP_LOGE(TAG, "I2C probe bus fault at 0x%02X: %s", (unsigned)a, esp_err_to_name(pe));
                i2c_del_master_bus(bus);
                return pe;
            }
            if (ack) {
                found = true;
                found_addr = (uint8_t)a;
                session->i2c_ack_seen = true;
                ESP_LOGI(TAG, "I2C ACK at 0x%02X (full scan)", found_addr);
                break;
            }
        }
    }

    i2c_del_master_bus(bus);

    if (found) {
        session->bus = SESSION_BUS_I2C;
        session->i2c_addr_7bit = found_addr;
        printf("I2C device responded at 0x%02X — I2C path selected "
               "(pick SSD1306 vs SH1106 in panel setup if unsure).\n",
               found_addr);
    } else if (force_i2c) {
        session->bus = SESSION_BUS_I2C;
        session->i2c_addr_7bit = 0;
        printf("[identity] Forced I2C but no ACK — check SDA/SCL (addr unknown).\n");
    } else {
        session->bus = SESSION_BUS_SPI;
        session->i2c_addr_7bit = 0;
        printf("No I2C display identified — using SPI.\n");
    }

    return ESP_OK;
}
