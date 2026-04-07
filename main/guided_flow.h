#pragma once

#include <stdbool.h>

#include "session.h"

/*
 * Guided steps (internal indices 1..9): after overview Enter, flow starts at panel
 * setup (G3). G1 is wiring revisit only (R / P from panel setup).
 */
void guided_flow_run(test_session_t *session);

/* After successful identity: overview + Enter/E/!/@; false = outer boot restart. */
bool guided_show_overview_and_wait(test_session_t *session);
