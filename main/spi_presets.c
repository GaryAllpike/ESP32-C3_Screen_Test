#include "spi_presets.h"

static const spi_preset_t k_presets_st7735[] = {
    { 128, 128, "128x128" },
    { 128, 160, "128x160" },
    { 130, 160, "130x160 (wider GRAM / some modules)" },
    { 132, 160, "132x160" },
    { 132, 162, "132x162 (full ST7735 GRAM)" },
};
static const spi_preset_t k_presets_st7789[] = {
    { 128, 160, "128x160" },
    { 240, 240, "240x240" },
    { 240, 320, "240x320" },
    { 135, 240, "135x240" },
};
static const spi_preset_t k_presets_ili9341[] = {
    { 240, 320, "240x320" },
    { 320, 240, "320x240" },
};
static const spi_preset_t k_presets_ili9488[] = {
    { 320, 480, "320x480" },
};
static const spi_preset_t k_presets_gc9a01[] = {
    { 240, 240, "240x240 (round)" },
};
static const spi_preset_t k_presets_st7796[] = {
    { 320, 480, "320x480" },
    { 480, 320, "480x320" },
};

/* Phase 8.15: order matches manual menu (typical resolution bands); index = menu digit − 1. */
const spi_chip_desc_t k_spi_chips[] = {
    { SESSION_SPI_ST7735, "ST7735", "ST7735 / HSGT7735", 20 * 1000 * 1000, k_presets_st7735, 5 },
    { SESSION_SPI_ST7789, "ST7789", "ST7789V / 7789", 20 * 1000 * 1000, k_presets_st7789, 4 },
    { SESSION_SPI_GC9A01, "GC9A01", "GC9A01 / 9A01 round", 20 * 1000 * 1000, k_presets_gc9a01, 1 },
    { SESSION_SPI_ILI9341, "ILI9341", "ILI9341 / ILI9342", 20 * 1000 * 1000, k_presets_ili9341, 2 },
    { SESSION_SPI_ILI9488, "ILI9488", "ILI9488", 10 * 1000 * 1000, k_presets_ili9488, 1 },
    { SESSION_SPI_ST7796, "ST7796", "ST7796 (SPI, 3.5\" class)", 10 * 1000 * 1000, k_presets_st7796, 2 },
};

const size_t k_spi_n_spi_chips = sizeof(k_spi_chips) / sizeof(k_spi_chips[0]);

const spi_chip_desc_t *spi_presets_find_chip(session_spi_chip_t chip)
{
    for (size_t i = 0; i < k_spi_n_spi_chips; i++) {
        if (k_spi_chips[i].chip == chip) {
            return &k_spi_chips[i];
        }
    }
    return NULL;
}

void spi_presets_chip_gram_max(session_spi_chip_t chip, uint16_t *out_w, uint16_t *out_h)
{
    if (!out_w || !out_h) {
        return;
    }
    const spi_chip_desc_t *d = spi_presets_find_chip(chip);
    if (!d || d->n_presets == 0) {
        *out_w = 320;
        *out_h = 480;
        return;
    }
    uint16_t mw = 0, mh = 0;
    for (size_t i = 0; i < d->n_presets; i++) {
        if (d->presets[i].w > mw) {
            mw = d->presets[i].w;
        }
        if (d->presets[i].h > mh) {
            mh = d->presets[i].h;
        }
    }
    *out_w = mw;
    *out_h = mh;
}
