/* Version: 0.9 (Pre-Release) | JBG Discovery Framework */
#include "provision_print.h"
#include "board_pins.h"
#include "console_text.h"
#include "panel_hw.h"
#include "serial_menu.h"
#include "esp_err.h"
#include <inttypes.h>
#include <stdio.h>

static const char *spi_chip_name(session_spi_chip_t c)
{
    switch (c) {
    case SESSION_SPI_ST7735:
        return "ST7735";
    case SESSION_SPI_ST7789:
        return "ST7789";
    case SESSION_SPI_ILI9341:
        return "ILI9341";
    case SESSION_SPI_ILI9488:
        return "ILI9488";
    case SESSION_SPI_GC9A01:
        return "GC9A01";
    case SESSION_SPI_ST7796:
        return "ST7796";
    default:
        return "(none)";
    }
}

static const char *i2c_drv_name(session_i2c_driver_t d)
{
    switch (d) {
    case SESSION_I2C_DRV_SSD1306:
        return "SSD1306";
    case SESSION_I2C_DRV_SH1106:
        return "SH1106";
    default:
        return "(none)";
    }
}

/* Legacy §13 text block (also option [3] in provision_print_menu). */
static void provision_print_legacy_text_dump(const test_session_t *session)
{
    printf("\n======== Print config / provision (raw) ========\n\n");
    printf("Board: Tenstar Robot ESP32-C3 Super Mini + Expansion\n");
    printf("SPI: SCK=%d MOSI=%d RST=%d DC=%d CS=%d BL=%d MISO=NC\n",
           BOARD_DISPLAY_SPI_SCK, BOARD_DISPLAY_SPI_MOSI, BOARD_DISPLAY_SPI_RST,
           BOARD_DISPLAY_SPI_DC, BOARD_DISPLAY_SPI_CS, BOARD_DISPLAY_SPI_BL);
    printf("I2C: SDA=%d SCL=%d\n\n", BOARD_DISPLAY_I2C_SDA, BOARD_DISPLAY_I2C_SCL);

    if (session->bus == SESSION_BUS_I2C) {
        printf("Transport: I2C  addr=0x%02X  driver=%s\n", session->i2c_addr_7bit, i2c_drv_name(session->i2c_driver));
        if (session->panel_ready) {
            printf("\n");
            printf("Profile: %s\n", session->profile_tag);
            printf("Geometry: hor_res=%u ver_res=%u (SSD1306 height=%u)\n", (unsigned)session->hor_res,
                   (unsigned)session->ver_res, (unsigned)session->ssd1306_height);
            printf("Gap: col_offset=%d row_offset=%d\n", (int)session->gap_col, (int)session->gap_row);
            printf("Orientation: rot_quarter=%u mirror=(%d,%d) invert=%d\n", (unsigned)session->rot_quarter,
                   session->mirror_x, session->mirror_y, session->inv_en);
        }
    } else if (session->bus == SESSION_BUS_SPI) {
        printf("Transport: SPI  chip=%s  profile=%s\n", spi_chip_name(session->spi_chip), session->profile_tag);
        if (session->panel_ready) {
            printf("\n");
            printf("Geometry: hor_res=%u ver_res=%u\n", (unsigned)session->hor_res, (unsigned)session->ver_res);
            printf("PCLK operational: %" PRIu32 " Hz\n", session->spi_pclk_hz);
            if (session->peak_spi_hz > 0) {
                printf("Peak SPI (session): %" PRIu32 " Hz\n", session->peak_spi_hz);
            }
            printf("Backlight: %u%%\n", (unsigned)session->backlight_pct);
            printf("\n");
            printf("Gap: col_offset=%d row_offset=%d\n", (int)session->gap_col, (int)session->gap_row);
            printf("Orientation: rot_quarter=%u mirror=(%d,%d) invert=%d\n", (unsigned)session->rot_quarter,
                   session->mirror_x, session->mirror_y, session->inv_en);
            printf("\n");
            printf("SPI RGB element order: %s (panel create)\n",
                   session->spi_rgb_ele_order_rgb ? "RGB" : "BGR");
            printf("SPI logical primaries RGB565 (RED GREEN BLUE on glass): 0x%04X 0x%04X 0x%04X\n",
                   (unsigned)session->spi_logical_rgb565[0], (unsigned)session->spi_logical_rgb565[1],
                   (unsigned)session->spi_logical_rgb565[2]);
            printf("MADCTL: derive from swap_xy/mirror/invert above per chip datasheet (not a single byte across vendors).\n");
        } else {
            printf("\n(Panel not initialized — complete panel setup first.)\n");
        }
    } else {
        printf("Transport: unknown\n");
    }
    printf("\n=============================================\n\n");
}

static const char *arduino_driver_define(session_spi_chip_t chip)
{
    switch (chip) {
    case SESSION_SPI_ST7735:
        return "ST7735_DRIVER";
    case SESSION_SPI_ST7789:
        return "ST7789_DRIVER";
    case SESSION_SPI_ILI9341:
        return "ILI9341_DRIVER";
    case SESSION_SPI_ILI9488:
        return "ILI9488_DRIVER";
    case SESSION_SPI_GC9A01:
        return "GC9A01_DRIVER";
    case SESSION_SPI_ST7796:
        return "/* ST7796 — set TFT_eSPI driver per your fork / module */";
    default:
        return "/* Select a driver manually */";
    }
}

/* G9: TFT_eSPI User_Setup.h fragment from session (phys GRAM + driver; CGRAM note if gap non-zero). */
static void provision_print_tft_espi_user_setup_h_derived(const test_session_t *session, session_spi_chip_t chip)
{
    if (!session) {
        return;
    }
    printf("\n--- Arduino (TFT_eSPI) User_Setup.h ---\n");
    printf("#define TFT_WIDTH   %u\n", (unsigned)session->phys_w);
    printf("#define TFT_HEIGHT  %u\n", (unsigned)session->phys_h);
    printf("#define %s\n", arduino_driver_define(chip));
    if (session->spi_pclk_hz > 0) {
        printf("#define SPI_FREQUENCY  %" PRIu32 "\n", session->spi_pclk_hz);
    }
    if (session->gap_col != 0 || session->gap_row != 0) {
        printf("#define CGRAM_OFFSET // Note: Manual offset required for X=%d, Y=%d\n", (int)session->gap_col,
               (int)session->gap_row);
    }
}

static void print_target_arduino_tft_espi(const test_session_t *session)
{
    if (session->bus == SESSION_BUS_I2C) {
        printf("\nNote: TFT_eSPI is optimized for SPI TFTs. For I2C OLEDs on Arduino, we recommend the "
               "Adafruit_SSD1306 or U8g2 libraries. Your I2C address is: 0x%02X\n\n",
               (unsigned)session->i2c_addr_7bit);
        return;
    }

    if (session->bus != SESSION_BUS_SPI || !session->panel_ready || session->spi_chip == SESSION_SPI_CHIP_NONE) {
        printf("\nArduino TFT_eSPI snippet applies to an initialized SPI TFT session only.\n\n");
        return;
    }

    console_clear_screen();
    printf("// ========================================================================\n");
    printf("// TFT_eSPI User_Setup.h fragment — merge into your sketch or User_Setup.h\n");
    printf("// Board pins from board_pins.h (Tenstar ESP32-C3 Super Mini + expansion)\n");
    printf("// ========================================================================\n\n");

    printf("#define USER_SETUP_LOADED\n\n");
    printf("/* Controller (Bodmer TFT_eSPI) */\n");
    printf("#define %s\n\n", arduino_driver_define(session->spi_chip));

    if (session->spi_chip == SESSION_SPI_ST7735) {
        printf("/* ST7735: pick green/red/black tab init in TFT_eSPI if colours/offset look wrong */\n\n");
    }

    printf("/* SPI pins (ESP32-C3) */\n");
    printf("#define TFT_MOSI %d\n", BOARD_DISPLAY_SPI_MOSI);
    printf("#define TFT_SCLK %d\n", BOARD_DISPLAY_SPI_SCK);
    printf("#define TFT_CS   %d\n", BOARD_DISPLAY_SPI_CS);
    printf("#define TFT_DC   %d\n", BOARD_DISPLAY_SPI_DC);
    printf("#define TFT_RST  %d\n", BOARD_DISPLAY_SPI_RST);
    printf("#define TFT_BL   %d\n", BOARD_DISPLAY_SPI_BL);
    printf("#define TFT_MISO %d\n", BOARD_DISPLAY_SPI_MISO);

    if (session->spi_pclk_hz > 0) {
        printf("\n#define SPI_FREQUENCY  %" PRIu32 "  /* session PCLK Hz */\n", session->spi_pclk_hz);
    } else {
        printf("\n#define SPI_FREQUENCY  20000000\n");
    }

    printf("\n#define TFT_WIDTH   %u\n", (unsigned)session->hor_res);
    printf("#define TFT_HEIGHT  %u\n", (unsigned)session->ver_res);

    printf("\n/* CGRAM offset (esp_lcd gap_col / gap_row from bench) */\n");
    printf("#define CGRAM_OFFSET  %d,%d  /* col, row — split for TFT_eSPI setViewport / offsets as needed */\n",
           (int)session->gap_col, (int)session->gap_row);
    printf("#define CGRAM_OFFSET_COL  %d\n", (int)session->gap_col);
    printf("#define CGRAM_OFFSET_ROW  %d\n", (int)session->gap_row);

    printf("\n/* Display inversion (session inv_en) */\n");
    if (session->inv_en) {
        printf("#define TFT_INVERSION_ON\n");
    } else {
        printf("#define TFT_INVERSION_OFF\n");
    }

    printf("\n/* RGB/BGR bus order — verify against panel; session used %s for esp_lcd */\n",
           session->spi_rgb_ele_order_rgb ? "RGB" : "BGR");
    printf("/* TFT_eSPI: use TFT_RGB_ORDER if your driver supports it, or swap in draw colour */\n\n");

    printf("/* Orientation hints from bench: rot_quarter=%u mirror_x=%d mirror_y=%d */\n",
           (unsigned)session->rot_quarter, session->mirror_x ? 1 : 0, session->mirror_y ? 1 : 0);
    printf("/* Apply via setRotation() / MADCTL in your sketch — not auto-generated here. */\n\n");
    printf("// ========================================================================\n\n");
}

static uint32_t ili9488_suggested_buffer_bytes(const test_session_t *session)
{
    uint32_t w = session->hor_res;
    uint32_t h = session->ver_res;
    if (w == 0 || h == 0) {
        return 1024;
    }
    uint32_t sz = w * 24u * 3u;
    if (sz < 1024u) {
        sz = 1024u;
    }
    return sz;
}

static void print_target_esp_idf_lcd(const test_session_t *session)
{
    if (!session->panel_ready) {
        printf("\nESP-IDF esp_lcd snippet needs an initialized panel (complete panel setup first).\n\n");
        return;
    }

    if (session->bus == SESSION_BUS_I2C) {
        if (session->i2c_driver == SESSION_I2C_DRV_NONE) {
            printf("\nESP-IDF I2C OLED snippet: select SSD1306 or SH1106 in panel setup first.\n\n");
            return;
        }

        console_clear_screen();
        printf("// ========================================================================\n");
        printf("// ESP-IDF esp_lcd — I2C OLED (from bench session)\n");
        printf("// Geometry: %u x %u, addr 0x%02X, driver %s\n", (unsigned)session->hor_res,
               (unsigned)session->ver_res, (unsigned)session->i2c_addr_7bit, i2c_drv_name(session->i2c_driver));
        printf("// ========================================================================\n\n");

        printf("#include \"driver/i2c_master.h\"\n");
        printf("#include \"esp_lcd_panel_io.h\"\n");
        printf("#include \"esp_lcd_panel_ops.h\"\n");
        printf("#include \"esp_lcd_panel_ssd1306.h\"\n");
        printf("#include \"esp_lcd_panel_sh1106.h\"\n\n");

        printf("i2c_master_bus_config_t i2c_bus_cfg = {\n");
        printf("    .i2c_port = I2C_NUM_0,\n");
        printf("    .sda_io_num = BOARD_DISPLAY_I2C_SDA,\n");
        printf("    .scl_io_num = BOARD_DISPLAY_I2C_SCL,\n");
        printf("    .clk_source = I2C_CLK_SRC_DEFAULT,\n");
        printf("    .glitch_ignore_cnt = 7,\n");
        printf("    .flags.enable_internal_pullup = true,\n");
        printf("};\n");
        printf("i2c_master_bus_handle_t i2c_bus = NULL;\n");
        printf("ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_cfg, &i2c_bus));\n\n");

        printf("esp_lcd_panel_io_i2c_config_t io_cfg = {\n");
        printf("    .dev_addr = 0x%02X,\n", (unsigned)session->i2c_addr_7bit);
        printf("    .on_color_trans_done = NULL,\n");
        printf("    .user_ctx = NULL,\n");
        printf("    .control_phase_bytes = 1,\n");
        printf("    .dc_bit_offset = 6,\n");
        printf("    .lcd_cmd_bits = 8,\n");
        printf("    .lcd_param_bits = 8,\n");
        printf("    .flags = { .dc_low_on_data = false, .disable_control_phase = false },\n");
        printf("    .scl_speed_hz = 400 * 1000,\n");
        printf("};\n");
        printf("esp_lcd_panel_io_handle_t io_handle = NULL;\n");
        printf("ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c(i2c_bus, &io_cfg, &io_handle));\n\n");

        printf("esp_lcd_panel_dev_config_t panel_config = {\n");
        printf("    .bits_per_pixel = 1,\n");
        printf("    .reset_gpio_num = -1,\n");
        printf("};\n");
        printf("esp_lcd_panel_handle_t panel_handle = NULL;\n\n");

        if (session->i2c_driver == SESSION_I2C_DRV_SSD1306) {
            printf("esp_lcd_panel_ssd1306_config_t ssd1306_cfg = { .height = %u };\n",
                   (unsigned)session->ssd1306_height);
            printf("panel_config.vendor_config = &ssd1306_cfg;\n");
            printf("ESP_ERROR_CHECK(esp_lcd_new_panel_ssd1306(io_handle, &panel_config, &panel_handle));\n\n");
        } else {
            printf("/* SH1106: add tny-robotics__sh1106-esp-idf (or equivalent) to the project */\n");
            printf("panel_config.vendor_config = NULL;\n");
            printf("ESP_ERROR_CHECK(esp_lcd_new_panel_sh1106(io_handle, &panel_config, &panel_handle));\n\n");
        }

        printf("ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));\n");
        printf("ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));\n");
        printf("ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true));\n");
        printf("ESP_ERROR_CHECK(esp_lcd_panel_set_gap(panel_handle, %d, %d));\n", (int)session->gap_col,
               (int)session->gap_row);
        printf("/* Bench gap: SH1106 often needs column gap 2 for 132->128 column mapping */\n");
        printf("// ========================================================================\n\n");
        return;
    }

    if (session->bus != SESSION_BUS_SPI || session->spi_chip == SESSION_SPI_CHIP_NONE) {
        printf("\nESP-IDF esp_lcd TFT snippet applies to an initialized SPI session with a known chip.\n\n");
        return;
    }

    console_clear_screen();
    printf("// ========================================================================\n");
    printf("// ESP-IDF esp_lcd — panel device config + factory call (copy into your project)\n");
    printf("// Pins use board_pins.h macros; values mirror bench session.\n");
    printf("// ========================================================================\n\n");

    const char *rgb_macro =
        session->spi_rgb_ele_order_rgb ? "LCD_RGB_ELEMENT_ORDER_RGB" : "LCD_RGB_ELEMENT_ORDER_BGR";

    if (session->spi_chip == SESSION_SPI_ILI9488) {
        uint32_t buf_sz = ili9488_suggested_buffer_bytes(session);
        printf("#include \"esp_lcd_panel_io.h\"\n");
        printf("#include \"esp_lcd_panel_ops.h\"\n");
        printf("#include \"esp_lcd_panel_vendor.h\"\n");
        printf("#include \"esp_lcd_ili9488.h\"\n");
        printf("#include \"esp_lcd_types.h\"\n");
        printf("#include \"driver/gpio.h\"\n\n");

        printf("esp_lcd_panel_dev_config_t panel_config = {\n");
        printf("    .reset_gpio_num = BOARD_DISPLAY_SPI_RST,\n");
        printf("    .rgb_ele_order = %s,\n", rgb_macro);
        printf("    .bits_per_pixel = 18,\n");
        printf("    .vendor_config = NULL,\n");
        printf("};\n\n");
        printf("esp_lcd_panel_handle_t panel_handle = NULL;\n");
        printf("esp_err_t err = esp_lcd_new_panel_ili9488(io_handle, &panel_config, %u, &panel_handle);\n",
               (unsigned)buf_sz);
        printf("/* ILI9488: vendor buffer size %" PRIu32 " bytes (same order of magnitude as bench firmware). */\n",
               (uint32_t)buf_sz);
    } else {
        printf("#include \"esp_lcd_panel_io.h\"\n");
        printf("#include \"esp_lcd_panel_ops.h\"\n");
        printf("#include \"esp_lcd_types.h\"\n");
        printf("#include \"driver/gpio.h\"\n");
        switch (session->spi_chip) {
        case SESSION_SPI_ST7735:
            printf("#include \"esp_lcd_st7735.h\"\n\n");
            break;
        case SESSION_SPI_ST7789:
            printf("#include \"esp_lcd_panel_st7789.h\"\n");
            printf("/* Also link esp_lcd ST7789 vendor component in CMake / idf_component_register REQUIRES */\n\n");
            break;
        case SESSION_SPI_ILI9341:
            printf("#include \"esp_lcd_ili9341.h\"\n\n");
            break;
        case SESSION_SPI_GC9A01:
            printf("#include \"esp_lcd_gc9a01.h\"\n\n");
            break;
        case SESSION_SPI_ST7796:
            printf("/* TODO: Component Required — add esp_lcd ST7796 (vendor) or compatible driver; not in ESP-IDF tree here */\n\n");
            break;
        default:
            printf("\n");
            break;
        }

        printf("esp_lcd_panel_dev_config_t panel_config = {\n");
        printf("    .reset_gpio_num = BOARD_DISPLAY_SPI_RST,\n");
        printf("    /* ESP-IDF uses rgb_ele_order (not a separate color_space enum on this struct) */\n");
        printf("    .rgb_ele_order = %s,\n", rgb_macro);
        printf("    .bits_per_pixel = 16,\n");
        if (session->spi_chip == SESSION_SPI_ST7735) {
            printf("    /* ST7735: verify vendor init block / tab against your glass (waveshare fork in this bench project) */\n");
            printf("    .vendor_config = NULL,  /* TODO: st7735_vendor_config_t if you override init */\n");
        } else {
            printf("    .vendor_config = NULL,\n");
        }
        printf("};\n\n");
        printf("esp_lcd_panel_handle_t panel_handle = NULL;\n");

        switch (session->spi_chip) {
        case SESSION_SPI_ST7735:
            printf("esp_err_t err = esp_lcd_new_panel_st7735(io_handle, &panel_config, &panel_handle);\n");
            break;
        case SESSION_SPI_ST7789:
            printf("esp_err_t err = esp_lcd_new_panel_st7789(io_handle, &panel_config, &panel_handle);\n");
            break;
        case SESSION_SPI_ILI9341:
            printf("esp_err_t err = esp_lcd_new_panel_ili9341(io_handle, &panel_config, &panel_handle);\n");
            break;
        case SESSION_SPI_GC9A01:
            printf("esp_err_t err = esp_lcd_new_panel_gc9a01(io_handle, &panel_config, &panel_handle);\n");
            break;
        case SESSION_SPI_ST7796:
            printf("/* TODO: Component Required — esp_lcd_new_panel_st7796() when available */\n");
            printf("esp_err_t err = ESP_ERR_NOT_SUPPORTED;\n");
            break;
        default:
            printf("esp_err_t err = ESP_ERR_NOT_SUPPORTED;\n");
            break;
        }
    }

    printf("\n/* Geometry from session: width=%u height=%u — set via panel init / draw window per driver API */\n",
           (unsigned)session->hor_res, (unsigned)session->ver_res);
    printf("/* Gap: col=%d row=%d — esp_lcd_panel_set_gap() after init if needed */\n", (int)session->gap_col,
           (int)session->gap_row);
    printf("/* PCLK used on bench: %" PRIu32 " Hz */\n\n", session->spi_pclk_hz);
    printf("// ========================================================================\n\n");
}

/* ST77xx-class MADCTL mirror bits (same mapping as panel_hw silicon basis). */
#define JBG_MADCTL_MX 0x40u
#define JBG_MADCTL_MY 0x80u

static uint8_t provision_silicon_mirror_mxmy_byte(panel_mirror_t m)
{
    switch (m) {
    case PANEL_MIRROR_XY:
        return (uint8_t)(JBG_MADCTL_MX | JBG_MADCTL_MY);
    case PANEL_MIRROR_X:
        return (uint8_t)JBG_MADCTL_MX;
    case PANEL_MIRROR_Y:
        return (uint8_t)JBG_MADCTL_MY;
    case PANEL_MIRROR_NONE:
    default:
        return 0;
    }
}

static const char *provision_silicon_mirror_desc(panel_mirror_t m)
{
    switch (m) {
    case PANEL_MIRROR_XY:
        return "MX+MY (both)";
    case PANEL_MIRROR_X:
        return "MX only";
    case PANEL_MIRROR_Y:
        return "MY only";
    case PANEL_MIRROR_NONE:
    default:
        return "none";
    }
}

void provision_print_st7735_profile(const test_session_t *session)
{
    if (!session) {
        return;
    }

    uint8_t mir = provision_silicon_mirror_mxmy_byte(session->silicon_mirror);

    printf("\n-------- ST7735 profile (JBG discovery) --------\n");
    printf("Resolved dimensions: %u x %u\n", (unsigned)session->phys_w, (unsigned)session->phys_h);
    printf("Gap / offsets: col=%d row=%d\n", (int)session->gap_col, (int)session->gap_row);
    printf("Mirror mode (silicon basis / MADCTL-class): %s  (MX/MY bits = 0x%02X)\n",
           provision_silicon_mirror_desc(session->silicon_mirror), (unsigned)mir);
    printf("Inversion: %s\n", session->inv_en ? "ON" : "OFF");
    printf("--------\n");
    printf("FINAL_INIT_STRING (paste into your target firmware header):\n\n");
    {
        uint8_t madctl_full = (uint8_t)(mir | (session->spi_rgb_ele_order_rgb ? 0x08u : 0u));
        printf("#define JBG_CONFIG_ST7735 { .w=%u, .h=%u, .x_gap=%d, .y_gap=%d, .mirror=0x%02X } "
               "/* MADCTL: MX|MY silicon basis 0x%02X; full MADCTL-style byte 0x%02X (incl. RGB order bit) */\n\n",
               (unsigned)session->phys_w, (unsigned)session->phys_h, (int)session->gap_col,
               (int)session->gap_row, (unsigned)mir, (unsigned)mir, (unsigned)madctl_full);
    }
    printf("(Define a matching struct type in your project, or wrap this in a factory macro.)\n");

    provision_print_tft_espi_user_setup_h_derived(session, SESSION_SPI_ST7735);
    printf("/* Full pin map + inversion: provision menu [2] */\n\n");
}

void provision_print_st7789_profile(const test_session_t *session)
{
    if (!session) {
        return;
    }
    printf("\nST7789 Profile: [W:%u H:%u GapX:%d GapY:%d]\n", (unsigned)session->phys_w,
           (unsigned)session->phys_h, (int)session->gap_col, (int)session->gap_row);
    provision_print_tft_espi_user_setup_h_derived(session, SESSION_SPI_ST7789);
    printf("/* Full pin map + inversion: provision menu [2] */\n\n");
}

void provision_print_generic_spi_profile(const test_session_t *session)
{
    if (!session) {
        return;
    }
    printf("\n-------- Generic SPI profile (JBG) --------\n");
    printf("Controller: %s\n", spi_chip_name(session->spi_chip));
    printf("Resolved:   W=%u H=%u | Gap X=%d Y=%d\n", (unsigned)session->phys_w, (unsigned)session->phys_h,
           (int)session->gap_col, (int)session->gap_row);
    printf("Session:    rot_quarter=%u mirror=(%d,%d) invert=%d | PCLK %" PRIu32 " Hz\n",
           (unsigned)session->rot_quarter, session->mirror_x ? 1 : 0, session->mirror_y ? 1 : 0,
           session->inv_en ? 1 : 0, session->spi_pclk_hz);
    printf("(Chip-specific JBG_CONFIG_* / init-string not wired yet — use [3] raw dump or menu [1] ESP-IDF.)\n");
    printf("--------------------------------------------\n\n");
}

void provision_print_profile_dispatch(const test_session_t *session)
{
    if (!session) {
        return;
    }
    /* Chip dispatch: extend switch + provision_print_*_profile per controller. */
    if (session->bus != SESSION_BUS_SPI || session->spi_chip == SESSION_SPI_CHIP_NONE) {
        provision_print_session_summary(session);
        return;
    }
    switch (session->spi_chip) {
    case SESSION_SPI_ST7735:
        provision_print_st7735_profile(session);
        break;
    case SESSION_SPI_ST7789:
        provision_print_st7789_profile(session);
        break;
    case SESSION_SPI_ILI9341:
    case SESSION_SPI_ILI9488:
    case SESSION_SPI_GC9A01:
    case SESSION_SPI_ST7796:
        provision_print_generic_spi_profile(session);
        break;
    default:
        provision_print_session_summary(session);
        break;
    }
}

void provision_print_session_summary(const test_session_t *session)
{
    if (session) {
        provision_print_legacy_text_dump(session);
    }
}

void provision_print_menu(test_session_t *session)
{
    if (!session) {
        return;
    }

    for (;;) {
        printf("\n======== Provision / code generator ========\n\n");
        printf("  [1] ESP-IDF (esp_lcd) C snippet\n");
        printf("  [2] Arduino TFT_eSPI User_Setup.h fragment\n");
        printf("  [3] Raw text dump (legacy)\n");
        printf("  [4] Batch test next display (same config) — safe swap, then verify\n");
        printf("\nChoice (Enter = exit): ");
        int c = serial_read_menu_choice("1234\n", session);
        if (c == SERIAL_KEY_APP_RESTART) {
            return;
        }
        if (c == SERIAL_KEY_DISPLAY_RECOVERED) {
            continue;
        }
        if (c == SERIAL_KEY_ENTER) {
            printf("\n");
            return;
        }
        if (c == '1') {
            print_target_esp_idf_lcd(session);
        } else if (c == '2') {
            print_target_arduino_tft_espi(session);
        } else if (c == '3') {
            provision_print_legacy_text_dump(session);
        } else if (c == '4') {
            if (!session->panel_ready) {
                printf("\nComplete panel setup first (no batch swap without a calibrated profile).\n");
                continue;
            }
            panel_hw_safe_swap_pause();
            printf("\n--- SAFE SWAP MODE ---\nPins are Hi-Z. Swap your display now.\nPress [Enter] to re-initialize and verify.\n");
            if (serial_wait_enter_hooks("Press [Enter] when ready: ", session) == SERIAL_WAIT_ENTER_BOOT_RESTART) {
                return;
            }
            esp_err_t err = panel_hw_reinit_from_session(session);
            if (err != ESP_OK) {
                printf("Re-init failed (%s). Use @ after a snapshot, !, or panel setup to recover.\n", esp_err_to_name(err));
                continue;
            }
            printf("[Panel re-initialized — session calibration re-applied. Next: calibration verification.]\n");
            session->batch_dot_opens_provision = true;
            session->start_g10_after_provision = true;
            printf("\n");
            return;
        }
    }
}
