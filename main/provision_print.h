/* Version: 0.9 (Pre-Release) | JBG Discovery Framework */
#ifndef PROVISION_PRINT_H
#define PROVISION_PRINT_H

#include "session.h"

void provision_print_menu(test_session_t *session);
void provision_print_session_summary(const test_session_t *session);
void provision_print_st7735_profile(const test_session_t *session);
void provision_print_st7789_profile(const test_session_t *session);
void provision_print_generic_spi_profile(const test_session_t *session);
void provision_print_profile_dispatch(const test_session_t *session);

#endif
