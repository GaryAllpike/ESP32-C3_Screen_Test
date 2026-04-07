# MINIMAL_BUILD: only these + transitive IDF deps are compiled.
idf_component_register(SRCS                             "app_main.c"
                            "console_dashboard.c"
                            "console_text.c"
                            "appshell.c"
                            "display_recovery.c"
                            "stage_panel_init.c"
                            "stage_2_orientation.c"
                            "stage_3_probe.c"
                            "stage_4_verify.c"
                            "stage_5_validation.c"
                            "stage_4_provision.c"
                            "spi_presets.c"
                            "stage_display_adjust.c"
                            "stage_patterns.c"
                            "guided_flow.c"
                            "guided_ui_strings.c"
                            "provision_print.c"
                            "hookup_print.c"
                            "identity.c"
                            "panel_hw.c"
                            "panel_assets.c"
                            "panel_probes.c"
                            "safe_idle.c"
                            "serial_menu.c"
                            "session.c"
                    INCLUDE_DIRS "."
                    REQUIRES esp_driver_gpio
                             esp_driver_uart
                             esp_driver_i2c
                             esp_driver_spi
                             esp_driver_ledc
                             freertos
                             log
                             esp_lcd
                             espressif__esp_lcd_ili9341
                             espressif__esp_lcd_gc9a01
                             waveshare__esp_lcd_st7735
                             atanisoft__esp_lcd_ili9488
                             tny-robotics__sh1106-esp-idf)
