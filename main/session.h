/* Version: 0.9 (Pre-Release) | JBG Discovery Framework */
#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    SESSION_BUS_UNKNOWN = 0,
    SESSION_BUS_SPI,
    SESSION_BUS_I2C,
} session_bus_t;

typedef enum {
    SESSION_TRANSPORT_AUTO = 0,
    SESSION_TRANSPORT_FORCE_SPI,
    SESSION_TRANSPORT_FORCE_I2C,
} session_transport_override_t;

typedef enum {
    SESSION_SPI_CHIP_NONE = 0,
    SESSION_SPI_ST7735,
    SESSION_SPI_ST7789,
    SESSION_SPI_ILI9341,
    SESSION_SPI_ILI9488,
    SESSION_SPI_GC9A01,
    SESSION_SPI_ST7796,
} session_spi_chip_t;

typedef enum {
    SESSION_I2C_DRV_NONE = 0,
    SESSION_I2C_DRV_SSD1306,
    SESSION_I2C_DRV_SH1106,
} session_i2c_driver_t;

/* Stage 3 silicon basis; panel_hw maps to controller MADCTL / mirror. */
typedef enum {
    PANEL_MIRROR_NONE = 0,
    PANEL_MIRROR_X,
    PANEL_MIRROR_Y,
    PANEL_MIRROR_XY,
} panel_mirror_t;

typedef struct {
    session_bus_t bus;
    uint8_t i2c_addr_7bit;
    bool i2c_ack_seen;
    session_transport_override_t transport_override;

    bool panel_ready;
    session_spi_chip_t spi_chip;
    session_i2c_driver_t i2c_driver;
    char profile_tag[48];
    uint16_t hor_res;
    uint16_t ver_res;
    /* Controller GRAM extent / silicon caps vs visible framebuffer. */
    uint16_t phys_w;
    uint16_t phys_h;
    panel_mirror_t silicon_mirror;
    uint8_t silicon_extent_red_hits_right;
    int16_t gap_col;
    int16_t gap_row;
    uint8_t rot_quarter;
    bool mirror_x;
    bool mirror_y;
    bool inv_en;
    /* SPI RGB565: MADCTL MV/MX/MY/RGB bits matching panel_hw_apply_orientation; session is SoT for UI. */
    uint8_t madctl;
    bool spi_rgb_ele_order_rgb;
    uint16_t spi_logical_rgb565[3];
    uint8_t ssd1306_height;
    uint8_t backlight_pct;
    uint32_t spi_pclk_hz;
    uint32_t peak_spi_hz;

    bool batch_dot_opens_provision;
    bool start_g10_after_provision;
    uint8_t guided_override_next_stage;
} test_session_t;

void session_init(test_session_t *s);

void session_sync_mirror_from_silicon(test_session_t *s);

void session_reset_display_fields(test_session_t *s);

/* Short model string for dashboard (SPI chip or I2C driver, or "—"). */
const char *session_model_label(const test_session_t *s);

void session_print_display_truth(const test_session_t *s, const char *where_label);
void session_print_display_truth_no_orient(const test_session_t *s, const char *where_label);

void session_remap_gaps_after_orient_key(test_session_t *s, int key);
