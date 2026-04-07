#pragma once

#include "session.h"
#include <stdint.h>

/* Phase 4: UI colours from session logical primaries only (r, g, b, r|g, g|b, r|b, r|g|b) + absolute black.
 * NULL session uses canonical RGB565 primaries (same as spi_session_logical_rgb fallback). */

static inline void ui_rgb565_primaries(const test_session_t *s, uint16_t *r, uint16_t *g, uint16_t *b)
{
    if (s) {
        *r = s->spi_logical_rgb565[0];
        *g = s->spi_logical_rgb565[1];
        *b = s->spi_logical_rgb565[2];
    } else {
        *r = 0xF800u;
        *g = 0x07E0u;
        *b = 0x001Fu;
    }
}

static inline uint16_t ui_color_bg(const test_session_t *s)
{
    (void)s;
    return 0x0000u;
}

static inline uint16_t ui_color_primary_r(const test_session_t *s)
{
    uint16_t r, g, b;
    ui_rgb565_primaries(s, &r, &g, &b);
    return r;
}

static inline uint16_t ui_color_primary_g(const test_session_t *s)
{
    uint16_t r, g, b;
    ui_rgb565_primaries(s, &r, &g, &b);
    return g;
}

static inline uint16_t ui_color_primary_b(const test_session_t *s)
{
    uint16_t r, g, b;
    ui_rgb565_primaries(s, &r, &g, &b);
    return b;
}

static inline uint16_t ui_color_text_normal(const test_session_t *s)
{
    uint16_t r, g, b;
    ui_rgb565_primaries(s, &r, &g, &b);
    return (uint16_t)(r | g | b);
}

static inline uint16_t ui_color_highlight(const test_session_t *s)
{
    uint16_t r, g, b;
    ui_rgb565_primaries(s, &r, &g, &b);
    return (uint16_t)(r | g);
}

static inline uint16_t ui_color_alert(const test_session_t *s)
{
    return ui_color_primary_r(s);
}

static inline uint16_t ui_color_cyan(const test_session_t *s)
{
    uint16_t r, g, b;
    ui_rgb565_primaries(s, &r, &g, &b);
    return (uint16_t)(g | b);
}

static inline uint16_t ui_color_magenta(const test_session_t *s)
{
    uint16_t r, g, b;
    ui_rgb565_primaries(s, &r, &g, &b);
    return (uint16_t)(r | b);
}

/* G5: nested 1 px rings — outer highlight, mid G|B blend, inner R|B blend. */
static inline uint16_t ui_color_g5_outer(const test_session_t *s)
{
    return ui_color_highlight(s);
}

static inline uint16_t ui_color_g5_mid(const test_session_t *s)
{
    uint16_t r, g, b;
    ui_rgb565_primaries(s, &r, &g, &b);
    return (uint16_t)(g | b);
}

static inline uint16_t ui_color_g5_inner(const test_session_t *s)
{
    uint16_t r, g, b;
    ui_rgb565_primaries(s, &r, &g, &b);
    return (uint16_t)(r | b);
}

static inline uint16_t ui_color_g5_interior(const test_session_t *s)
{
    return ui_color_bg(s);
}

/* SPI init flow: magenta full-screen after size check (logical r|b). */
static inline uint16_t ui_color_probe_fill(const test_session_t *s)
{
    return ui_color_magenta(s);
}
