#pragma once

#include "session.h"
#include <stddef.h>

typedef struct {
    uint16_t w;
    uint16_t h;
    const char *label;
} spi_preset_t;

typedef struct {
    session_spi_chip_t chip;
    const char *name;
    const char *markings;
    uint32_t default_pclk_hz;
    const spi_preset_t *presets;
    size_t n_presets;
} spi_chip_desc_t;

extern const spi_chip_desc_t k_spi_chips[];
extern const size_t k_spi_n_spi_chips;

/* Lookup manual / try-sequence chip row; NULL if unknown. */
const spi_chip_desc_t *spi_presets_find_chip(session_spi_chip_t chip);

/* Widest × tallest preset per chip (controller GRAM extent for full-panel wipe). */
void spi_presets_chip_gram_max(session_spi_chip_t chip, uint16_t *out_w, uint16_t *out_h);
