#pragma once

#include <stdbool.h>

/*
 * Operator-facing titles/blurbs for guided steps (internal stage indices 1..10).
 * No spec references or G-labels in user strings.
 */
typedef struct {
    const char *title;
    const char *blurb;
    const char *key_extra; /* optional third line for context (e.g. gap stage) */
} guided_stage_meta_t;

const guided_stage_meta_t *guided_stage_meta(unsigned stage_1_to_9);

void guided_print_post_identity_overview(void);

/* Main guided shell: context-sensitive key lines; width is CONSOLE_TEXT_COLUMNS in console_text.h. */
void guided_print_shell_key_help(unsigned stage_1_to_9);
