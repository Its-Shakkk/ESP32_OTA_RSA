#ifndef ROLLBACK_MANAGER_H
#define ROLLBACK_MANAGER_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

void rollback_manager_startup(void);
void rollback_manager_confirm_image(void);
void rollback_manager_force_rollback(void);
bool rollback_manager_is_pending_verify(void);

/**
 * @brief Handle a failed self-test.
 * 
 * Increments the failure counter for the current firmware.
 * If the counter reaches MAX_FAILED_ATTEMPTS, the current version
 * is blacklisted in NVS. Then performs a rollback.
 */
void rollback_manager_handle_failed_image(void);

#ifdef __cplusplus
}
#endif

#endif