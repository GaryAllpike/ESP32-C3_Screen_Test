/* Turnip probe bitmaps — tools/gen_brand_turnip.py */
#pragma once

#include <stdint.h>

#define PROBE_MARKER_TURNIP_RGB565_W 80
#define PROBE_MARKER_TURNIP_RGB565_H 80
#define PROBE_MARKER_TURNIP_MONO_W 64
#define PROBE_MARKER_TURNIP_MONO_H 64

extern const uint16_t panel_asset_probe_marker_turnip_rgb565[PROBE_MARKER_TURNIP_RGB565_W * PROBE_MARKER_TURNIP_RGB565_H];
extern const uint8_t panel_asset_probe_marker_turnip_mono[(PROBE_MARKER_TURNIP_MONO_W * PROBE_MARKER_TURNIP_MONO_H) / 8];
