#include "console_text.h"
#include <stdio.h>
#include <string.h>

void console_init_ui_frame(void)
{
    fputs("\033[H", stdout);
    fputs("\033[J", stdout);
}

void console_clear_screen(void)
{
    console_init_ui_frame();
}

void console_cursor_home(void)
{
    fputs("\033[H", stdout);
}

void console_cursor_save(void)
{
    fputs("\033[s", stdout);
}

void console_cursor_restore_clear_below(void)
{
    fputs("\033[u", stdout);
    fputs("\033[0J", stdout);
}

static void print_segment(const char *prefix, const char *start, const char *end)
{
    size_t plen = strlen(prefix);
    if (plen >= CONSOLE_TEXT_COLUMNS) {
        prefix = "";
        plen = 0;
    }
    const size_t line_max = CONSOLE_TEXT_COLUMNS - plen;
    /* Never force a minimum chunk width larger than the space left after prefix — that
     * would exceed CONSOLE_TEXT_COLUMNS (bug when prefix is long). */
    const size_t L = line_max;

    const char *p = start;
    while (p < end) {
        while (p < end && *p == ' ') {
            p++;
        }
        if (p >= end) {
            break;
        }

        fputs(prefix, stdout);
        size_t col = 0;

        for (;;) {
            const char *wst = p;
            while (p < end && *p != ' ') {
                p++;
            }
            size_t wlen = (size_t)(p - wst);
            while (p < end && *p == ' ') {
                p++;
            }

            if (wlen == 0) {
                break;
            }

            if (wlen > L) {
                if (col > 0) {
                    putchar('\n');
                    fputs(prefix, stdout);
                    col = 0;
                }
                const char *w = wst;
                size_t rem = wlen;
                while (rem > 0) {
                    size_t chunk = rem > L ? L : rem;
                    fwrite(w, 1, chunk, stdout);
                    w += chunk;
                    rem -= chunk;
                    putchar('\n');
                    if (rem > 0) {
                        fputs(prefix, stdout);
                    }
                }
                col = 0;
                if (p >= end) {
                    return;
                }
                continue;
            }

            size_t need = wlen + (col > 0 ? 1u : 0u);
            if (need > L - col) {
                putchar('\n');
                fputs(prefix, stdout);
                col = 0;
                need = wlen;
            }

            if (col > 0) {
                putchar(' ');
                col++;
            }
            fwrite(wst, 1, wlen, stdout);
            col += wlen;

            if (p >= end) {
                putchar('\n');
                return;
            }
        }
        putchar('\n');
    }
}

void console_print_wrapped(const char *prefix, const char *text)
{
    if (!text) {
        return;
    }
    const char *seg = text;
    for (;;) {
        const char *nl = strchr(seg, '\n');
        if (!nl) {
            print_segment(prefix, seg, seg + strlen(seg));
            return;
        }
        if (nl == seg) {
            putchar('\n');
        } else {
            print_segment(prefix, seg, nl);
        }
        seg = nl + 1;
        if (*seg == '\0') {
            return;
        }
    }
}
