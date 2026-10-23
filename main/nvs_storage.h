#ifndef NVS_STORAGE_H
#define NVS_STORAGE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "esp_err.h"

esp_err_t nvs_storage_init(void);

esp_err_t nvs_save_failed_version(const char *version);
esp_err_t nvs_get_failed_version(char *version, size_t len);
esp_err_t nvs_clear_failed_version(void);

esp_err_t nvs_set_failed_count(uint8_t count);
uint8_t nvs_get_failed_count(void);
esp_err_t nvs_increment_failed_count(void);
esp_err_t nvs_reset_failed_count(void);

esp_err_t nvs_set_count_version(const char *version);
esp_err_t nvs_get_count_version(char *version, size_t len);

// --- NEW: Secure version (anti‑rollback) ---
esp_err_t nvs_set_secure_version(uint32_t version);
uint32_t nvs_get_secure_version(void);

// --- NEW: Download offset (for resume) ---
esp_err_t nvs_set_download_offset(size_t offset);
esp_err_t nvs_get_download_offset(size_t *offset);

void nvs_print_ota_info(void);

#ifdef __cplusplus
}
#endif

#endif