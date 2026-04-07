/* Version: 0.9 (Pre-Release) | JBG Discovery Framework */
#pragma once

#include <stdbool.h>

#include "session.h"

/*
 * Serial key sets for serial_read_menu_choice (literal \n = Enter).
 *   wasd — nudge column/row gap (G5/G6)
 *   , .  — revert submenu / confirm
 * Orientation (G4): R rotate 90°, A/D mirror X, W/S mirror Y, I invert
 */
#define STAGE_KEYS_G4_ORIENT            "\n" ".,rwasdi"
#define STAGE_KEYS_G5_GAP               "\n" ".,wasd"
#define STAGE_KEYS_G6_EXTENTS           "\n" ".,wasd"
#define STAGE_KEYS_G6_BL                "\n" ".,ws"

bool stage_2_run_orientation(test_session_t *s);
bool stage_3_run_probe(test_session_t *s);
bool stage_4_run_verification(test_session_t *s);
bool stage_5_run_validation_loop(test_session_t *s);
bool stage_5_validation_run(test_session_t *s);
bool stage_4_run_provision(test_session_t *s);

bool display_stage_g3(test_session_t *s);
bool display_stage_g5(test_session_t *s);
bool display_stage_g6(test_session_t *s);
bool display_stage_g8(test_session_t *s);
bool display_stage_g10(test_session_t *s);
