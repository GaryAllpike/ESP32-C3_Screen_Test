/* Version: 0.9 (Pre-Release) | JBG Discovery Framework */
#pragma once

#include "session.h"

/* G4 orientation keys: updates session, gap remap, panel_hw_set_* so madctl/inv_en stay SoT. */
void panel_probes_g4_dispatch_orientation_key(test_session_t *s, int k);
