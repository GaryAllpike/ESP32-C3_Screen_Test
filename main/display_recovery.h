#pragma once

#include "session.h"
#include <stdbool.h>

void display_recovery_invalidate(void);
void display_recovery_snapshot(const test_session_t *s);
void display_recovery_restore(test_session_t *s);
bool display_recovery_has_snapshot(void);
