/* Version: 0.9 (Pre-Release) | JBG Discovery Framework */
#pragma once

#include "session.h"
#include <stdint.h>

/* Default terminal geometry (DECSTBM / cursor math). */
#define DASHBOARD_TERM_ROWS 24
#define DASHBOARD_TERM_COLS 80

void dashboard_init(void);
void dashboard_refresh_header(const test_session_t *s);
void dashboard_refresh_footer(unsigned stage_1_to_10, const test_session_t *s);
void dashboard_body_home(void);
void dashboard_show_prompt(void);
