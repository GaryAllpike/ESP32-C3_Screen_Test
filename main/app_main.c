/*
 * ESP32-C3 display test suite — entry (SPEC.md v1.7).
 */
#include "appshell.h"
#include "sdkconfig.h"
#include "esp_log.h"
#include <stdio.h>

#if CONFIG_ESP_CONSOLE_UART_DEFAULT || CONFIG_ESP_CONSOLE_UART_CUSTOM
#include "driver/uart_vfs.h"
#endif

static const char *TAG = "main";

void app_main(void)
{
#if CONFIG_ESP_CONSOLE_UART_DEFAULT || CONFIG_ESP_CONSOLE_UART_CUSTOM
    uart_vfs_dev_register();
    uart_vfs_dev_use_driver(CONFIG_ESP_CONSOLE_UART_NUM);
#endif
    /* Unbuffered I/O so prompts appear immediately (UART VFS or USB-JTAG console). */
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stdin, NULL, _IONBF, 0);
    ESP_LOGI(TAG, "Display test firmware boot");
    appshell_run();
}
