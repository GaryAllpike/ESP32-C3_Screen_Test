#pragma once

/*
 * Serial UI width: one place defines column budget; prose is wrapped to fit.
 * Prefix is printed at the start of every output line (indent/hanging indent).
 */
#define CONSOLE_TEXT_COLUMNS 60

/* Home + erase display — call once at guided entry to anchor the terminal viewport. */
void console_init_ui_frame(void);

/* Clear entire display buffer and move cursor to home (ANSI). */
void console_clear_screen(void);

void console_print_wrapped(const char *prefix, const char *text);

/* Cursor control for fixed-layout serial UI (VT100-style). */
void console_cursor_home(void);
void console_cursor_save(void);
void console_cursor_restore_clear_below(void);
