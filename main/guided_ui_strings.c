/* Version: 0.9 (Pre-Release) | JBG Discovery Framework */
#include "guided_ui_strings.h"
#include "console_text.h"
#include <stdio.h>

/*
 * Index [0] unused; [1]..[10] match guided_stage_t in guided_flow.c.
 * G1/G2: placeholder slots (banners suppressed in guided_flow); wiring lives in hookup only.
 */
static const guided_stage_meta_t k_meta_unknown = {
    .title = "Unknown step",
    .blurb = "Invalid stage index — report as firmware bug.",
    .key_extra = NULL,
};

static const guided_stage_meta_t k_meta[] = {
    [0] = { NULL, NULL, NULL },
    [1] = {
        .title = "",
        .blurb = "",
        .key_extra = NULL,
    },
    [2] = {
        .title = "",
        .blurb = "",
        .key_extra = NULL,
    },
    [3] = {
        .title = "Panel setup",
        .blurb = "Pick SPI chip + size, or I2C driver — get a stable picture.",
        .key_extra = NULL,
    },
    [4] = {
        .title = "Orientation",
        .blurb = "Rotate, mirror, invert until the TOP mark and image look right.",
        .key_extra = NULL,
    },
    [5] = {
        .title = "Origin (gap)",
        .blurb = "Ballpark R/G/B/M bands at GRAM (0,0), then origin marker probe; wasd nudges gap.",
        .key_extra = "SPI RGB565: colour quiz + marker probe. I2C / 18 bpp: classic wasd gap only.",
    },
    [6] = {
        .title = "Extents & backlight",
        .blurb = "Nested preset rectangles, extent probe for hor_res/ver_res, then TFT backlight.",
        .key_extra = "SPI RGB565: M/C/G/R quiz + A/D width, W/S height. 18 bpp: backlight only.",
    },
    [7] = {
        .title = "Test patterns",
        .blurb = "Solid fills and checks to verify the pipeline.",
        .key_extra = NULL,
    },
    [8] = {
        .title = "SPI speed check",
        .blurb = "Raise SPI clock in steps if you want a faster stable rate.",
        .key_extra = NULL,
    },
    [9] = {
        .title = "Handoff / profile",
        .blurb = "Discovery complete: ST7735 macro on serial; other chips get full text dump. Return to earlier steps with comma.",
        .key_extra = NULL,
    },
    [10] = {
        .title = "Batch calibration check",
        .blurb = "White 1 px frame at logical edges; interior ~50% gray — spot panel-to-panel shift.",
        .key_extra = NULL,
    },
};

const guided_stage_meta_t *guided_stage_meta(unsigned stage_1_to_9)
{
    if (stage_1_to_9 == 0 || stage_1_to_9 > 10) {
        return &k_meta_unknown;
    }
    return &k_meta[stage_1_to_9];
}

void guided_print_post_identity_overview(void)
{
    printf("\n\n");
    printf("------------------------------------------------------------\n");
    printf("What happens next\n\n");
    console_print_wrapped(
        "",
        "- Initialize the panel (driver, size, or I2C driver)\n"
        "- Set orientation, then origin gap and extents/backlight on SPI RGB565\n"
        "- Run test patterns; optional SPI speed check (SPI TFT)\n"
        "- Handoff step: ST7735 copy-paste config macro on serial; full text dump for other panels\n"
        "\n"
        "Advanced: SPI or I2C overrides, print config, resume main path.\n"
        "Press A from here or later. Enter continues.\n"
        "\n"
        "!  full restart from wiring screen\n"
        "@  restore last known-good display after a snapshot\n");
    printf("\n------------------------------------------------------------\n\n");
}

void guided_print_shell_key_help(unsigned stage_1_to_9)
{
    /* Invalid index: generic mid-flow key lines (same as typical N/P/R/E/O/G). */
    if (stage_1_to_9 == 0 || stage_1_to_9 > 10) {
        stage_1_to_9 = 4;
    }

    printf("\nKeys (case-insensitive):\n");
    if (stage_1_to_9 >= 9) {
        printf("  . or Enter: back to panel setup   comma: previous step\n");
    } else if (stage_1_to_9 == 3) {
        printf("  . or Enter: next step   comma: revisit wiring\n");
    } else {
        printf("  . or Enter: next step   comma: previous step\n");
    }
    printf("  R: restart wiring   A: Advanced menu   O: print config\n");
    printf("  G: where you are   !  @  (same as hookup screen)\n");
    if (stage_1_to_9 == 5 && k_meta[5].key_extra) {
        console_print_wrapped("  ", k_meta[5].key_extra);
    }
}
