#include "serial_menu.h"
#include "display_recovery.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <sys/ioctl.h>

/*
 * Single-key menu reads do not echo the key (see serial_read_menu_choice).
 * Line entry (serial_read_line) echoes so operators can run with terminal
 * local echo off and still see typed text; hide single-key input on the host.
 */

typedef enum {
    SERIAL_HOOK_NONE = 0,
    SERIAL_HOOK_BOOT_RESTART,
    SERIAL_HOOK_DISPLAY_RECOVERED,
} serial_hook_result_t;

/*
 * Shared !/@ handling for wait loops and menu input.
 * menu_return_on_recover: true matches serial_read_menu_choice (@ returns recover code);
 * false matches serial_wait_enter_hooks / overview wait (@ restores only, stay in loop).
 */
static serial_hook_result_t serial_apply_global_hooks(int c, test_session_t *session, bool menu_return_on_recover,
                                                      bool at_as_menu_key)
{
    if (c == '!') {
        return SERIAL_HOOK_BOOT_RESTART;
    }
    if (c == '@' && session && !at_as_menu_key) {
        display_recovery_restore(session);
        if (menu_return_on_recover) {
            return SERIAL_HOOK_DISPLAY_RECOVERED;
        }
    }
    return SERIAL_HOOK_NONE;
}

void serial_wait_enter(const char *prompt)
{
    if (prompt) {
        printf("%s", prompt);
    }
    for (;;) {
        int c = getchar();
        if (c == EOF) {
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        if (c == '\r' || c == '\n') {
            break;
        }
    }
    printf("\n");
}

serial_wait_enter_result_t serial_wait_enter_hooks(const char *prompt, test_session_t *session)
{
    if (prompt) {
        printf("%s", prompt);
    }
    for (;;) {
        int c = getchar();
        if (c == EOF) {
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        if (c == '\r' || c == '\n') {
            printf("\n");
            return SERIAL_WAIT_ENTER_OK;
        }
        serial_hook_result_t h = serial_apply_global_hooks(c, session, false, false);
        if (h == SERIAL_HOOK_BOOT_RESTART) {
            printf("\n");
            return SERIAL_WAIT_ENTER_BOOT_RESTART;
        }
    }
}

serial_overview_wait_result_t serial_wait_continue_or_advanced(const char *prompt, test_session_t *session)
{
    if (prompt) {
        printf("%s", prompt);
    }
    for (;;) {
        int c = getchar();
        if (c == EOF) {
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        if (c == '\r' || c == '\n') {
            printf("\n");
            return SERIAL_OVERVIEW_CONTINUE;
        }
        if (c == 'a' || c == 'A') {
            printf("\n");
            return SERIAL_OVERVIEW_ADVANCED;
        }
        serial_hook_result_t h = serial_apply_global_hooks(c, session, false, false);
        if (h == SERIAL_HOOK_BOOT_RESTART) {
            printf("\n");
            return SERIAL_OVERVIEW_BOOT_RESTART;
        }
    }
}

int serial_read_menu_choice(const char *valid, test_session_t *session)
{
    for (;;) {
        int c = getchar();
        if (c == EOF) {
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        if (c == '\r' || c == '\n') {
            if (valid != NULL && valid[0] != '\0' && strchr(valid, '\n') != NULL) {
                printf("\n");
                return SERIAL_KEY_ENTER;
            }
            continue;
        }
        if (c == ' ' || c == '\t') {
            continue;
        }
        bool at_menu = (valid != NULL && valid[0] != '\0' && strchr(valid, '@') != NULL);
        serial_hook_result_t h = serial_apply_global_hooks(c, session, true, at_menu);
        if (h == SERIAL_HOOK_BOOT_RESTART) {
            printf("\n");
            return SERIAL_KEY_APP_RESTART;
        }
        if (h == SERIAL_HOOK_DISPLAY_RECOVERED) {
            printf("\n");
            return SERIAL_KEY_DISPLAY_RECOVERED;
        }
        if (c >= 'A' && c <= 'Z') {
            c = c - 'A' + 'a';
        }
        if (valid != NULL && valid[0] != '\0' && strchr(valid, c) == NULL) {
            continue;
        }
        printf("\n");
        return c;
    }
}

int serial_read_char(test_session_t *session)
{
    return serial_read_menu_choice(NULL, session);
}

int serial_read_menu_choice_yesno(test_session_t *session)
{
    int c = serial_read_menu_choice("\n" "yn", session);
    return (c == SERIAL_KEY_ENTER) ? 'y' : c;
}

int serial_read_menu_choice_ynq(test_session_t *session)
{
    int c = serial_read_menu_choice("\n" "ynq", session);
    return (c == SERIAL_KEY_ENTER) ? 'y' : c;
}

size_t serial_read_line(char *buf, size_t cap)
{
    if (cap == 0) {
        return 0;
    }
    size_t i = 0;
    for (;;) {
        int c = getchar();
        if (c == EOF) {
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        if (c == '\r' || c == '\n') {
            printf("\n");
            break;
        }
        if (c == '\b' || c == 0x7f) {
            if (i > 0) {
                i--;
                printf("\b \b");
            }
            continue;
        }
        if ((unsigned char)c < 0x20u) {
            continue;
        }
        if (i + 1 < cap) {
            buf[i++] = (char)c;
            /* Use printf so echo goes through the same stdout path as the rest of the UI (USB-JTAG console). */
            printf("%c", c);
        }
    }
    buf[i] = '\0';
    return i;
}

static void serial_stdin_ensure_blocking(void)
{
    static bool configured;
    if (configured) {
        return;
    }
    configured = true;
    /* USB Serial/JTAG console: CR/LF is already normalized by the VFS driver.
     * UART console builds may call uart_vfs_dev_port_set_rx_line_endings where UART VFS is registered. */
    int fd = fileno(stdin);
    if (fd < 0) {
        return;
    }
    int fl = fcntl(fd, F_GETFL, 0);
    if (fl >= 0) {
        (void)fcntl(fd, F_SETFL, fl & (int)~O_NONBLOCK);
    }
}

static char *trim_leading_spaces(char *s)
{
    while (*s == ' ' || *s == '\t') {
        s++;
    }
    return s;
}

static void strip_trailing_crlf(char *s)
{
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r')) {
        s[--n] = '\0';
    }
}

int serial_read_line_safe(char *buf, size_t cap, test_session_t *session)
{
    if (cap == 0) {
        return 0;
    }
    serial_stdin_ensure_blocking();

    for (;;) {
        if (fgets(buf, (int)cap, stdin) == NULL) {
            buf[0] = '\0';
            return 0;
        }
        strip_trailing_crlf(buf);
        char *t = trim_leading_spaces(buf);
        if (t[0] == '!' && t[1] == '\0') {
            return SERIAL_LINE_BOOT_RESTART;
        }
        if (t[0] == '@' && t[1] == '\0') {
            if (session) {
                display_recovery_restore(session);
            }
            printf("\n");
            continue;
        }
        if (t != buf) {
            memmove(buf, t, strlen(t) + 1);
        }
        return (int)strlen(buf);
    }
}

void serial_discard_buffered_console_input(void)
{
#ifdef FIONREAD
    int fd = fileno(stdin);
    if (fd < 0) {
        return;
    }
    for (;;) {
        int avail = 0;
        if (ioctl(fd, FIONREAD, &avail) != 0 || avail <= 0) {
            return;
        }
        while (avail-- > 0) {
            (void)getchar();
        }
    }
#endif
}
