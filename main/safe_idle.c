#include "safe_idle.h"
#include "board_pins.h"
#include "driver/gpio.h"
#include "esp_check.h"

static const char *TAG = "safe_idle";

static esp_err_t out_high(int gpio_num)
{
    if (gpio_num < 0) {
        return ESP_OK;
    }
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << gpio_num,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&io), TAG, "gpio_config %d", gpio_num);
    ESP_RETURN_ON_ERROR(gpio_set_level(gpio_num, 1), TAG, "set high %d", gpio_num);
    return ESP_OK;
}

static esp_err_t out_low(int gpio_num)
{
    if (gpio_num < 0) {
        return ESP_OK;
    }
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << gpio_num,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&io), TAG, "gpio_config %d", gpio_num);
    ESP_RETURN_ON_ERROR(gpio_set_level(gpio_num, 0), TAG, "set low %d", gpio_num);
    return ESP_OK;
}

esp_err_t safe_idle_configure_display_pins(void)
{
    /* CS inactive high, BL off (low), RST released high, DC low, SCK/MOSI low (SPI mode 0 idle). */
    ESP_RETURN_ON_ERROR(out_high(BOARD_DISPLAY_SPI_CS), TAG, "CS");
    ESP_RETURN_ON_ERROR(out_low(BOARD_DISPLAY_SPI_BL), TAG, "BL");
    ESP_RETURN_ON_ERROR(out_high(BOARD_DISPLAY_SPI_RST), TAG, "RST");
    ESP_RETURN_ON_ERROR(out_low(BOARD_DISPLAY_SPI_DC), TAG, "DC");
    ESP_RETURN_ON_ERROR(out_low(BOARD_DISPLAY_SPI_SCK), TAG, "SCK");
    ESP_RETURN_ON_ERROR(out_low(BOARD_DISPLAY_SPI_MOSI), TAG, "MOSI");
    return ESP_OK;
}
