#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "session.h"

/* serial_read_menu_choice: ! = full restart; @ = restore snapshot unless '@' appears in valid[] (menu key). */
#define SERIAL_KEY_APP_RESTART        (-2)
#define SERIAL_KEY_DISPLAY_RECOVERED  (-3)
/* Returned when Enter/\\r is pressed and valid[] includes a literal newline (see STAGE_KEYS_* macros). */
#define SERIAL_KEY_ENTER              ((int)'\n')

typedef enum {
    SERIAL_WAIT_ENTER_OK = 0,
    SERIAL_WAIT_ENTER_BOOT_RESTART,
} serial_wait_enter_result_t;

typedef enum {
    SERIAL_OVERVIEW_CONTINUE = 0,
    SERIAL_OVERVIEW_ADVANCED,
    SERIAL_OVERVIEW_BOOT_RESTART,
} serial_overview_wait_result_t;

void serial_wait_enter(const char *prompt);

/*
 * Wait for Enter; ignore other characters silently except:
 *   !  request full boot restart (return BOOT_RESTART)
 *   @  try display recovery if session non-NULL
 */
serial_wait_enter_result_t serial_wait_enter_hooks(const char *prompt, test_session_t *session);

/*
 * After identity overview: Enter = continue, A/a = Advanced, !/@ same as hooks above.
 * Other keys ignored until a recognized key.
 */
serial_overview_wait_result_t serial_wait_continue_or_advanced(const char *prompt, test_session_t *session);

/*
 * Read one menu key: skips whitespace; normalizes A–Z to a–z.
 * Does not echo the key. Prints a newline after an accepted key (or !/@) so the
 * next line of UI starts clean; use terminal local echo off to avoid duplicate glyphs.
 * Always: '!' -> SERIAL_KEY_APP_RESTART. '@' + session -> restore -> SERIAL_KEY_DISPLAY_RECOVERED,
 *   unless valid[] contains '@' (then '@' is a normal menu key — e.g. mirror toggle).
 * If valid is non-NULL: only characters appearing in valid[] are returned; others ignored silently.
 * Include a literal '\\n' in valid[] to accept Enter / \\r as SERIAL_KEY_ENTER.
 * If valid is NULL: any non-whitespace key is returned (after !/@ handling).
 */
int serial_read_menu_choice(const char *valid, test_session_t *session);

/* One key: same !/@ hooks as serial_read_menu_choice; valid NULL = any non-whitespace (after case fold). */
int serial_read_char(test_session_t *session);

/* y/n menus: Enter counts as 'y'. !/@ unchanged. */
int serial_read_menu_choice_yesno(test_session_t *session);
/* y/n/q: Enter counts as 'y'. */
int serial_read_menu_choice_ynq(test_session_t *session);

/* Echoes printable characters and backspace; ends with newline on Enter. */
size_t serial_read_line(char *buf, size_t cap);

/*
 * Blocking line read via VFS (fgets) after clearing O_NONBLOCK on stdin — no getchar polling.
 * Strips CR/LF. Trims leading spaces for hook detection only.
 *   !  alone (after trim) -> SERIAL_LINE_BOOT_RESTART
 *   @  alone -> display_recovery_restore(session) if session non-NULL, then read again
 * session may be NULL (@ is ignored).
 * Returns length (>= 0), or SERIAL_LINE_BOOT_RESTART.
 */
#define SERIAL_LINE_BOOT_RESTART (-1)
int serial_read_line_safe(char *buf, size_t cap, test_session_t *session);

/* Best-effort: drop bytes already in the console RX buffer (e.g. before a prompt). */
void serial_discard_buffered_console_input(void);
