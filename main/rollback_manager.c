/******************************************************************************
 * @file    rollback_manager.c
 * @brief   ESP32 OTA Rollback Manager with persistent blacklisting
 ******************************************************************************/

#include "rollback_manager.h"

#include <stdbool.h>
#include <string.h>

#include "esp_log.h"
#include "esp_ota_ops.h"
#include "app_config.h"
#include "nvs_storage.h"

static const char *TAG = "ROLLBACK";

/*==============================================================================
 * Startup
 *============================================================================*/

void rollback_manager_startup(void)
{
    const esp_partition_t *running = esp_ota_get_running_partition();

    if (running == NULL)
    {
        ESP_LOGE(TAG, "Cannot determine running partition");
        return;
    }

    esp_ota_img_states_t image_state;

    esp_err_t err = esp_ota_get_state_partition(running, &image_state);

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to read OTA image state : %s", esp_err_to_name(err)); 
        return;
    }

    ESP_LOGI(TAG, "======================================");
    ESP_LOGI(TAG, "Rollback Manager Started");
    ESP_LOGI(TAG, "Running Partition : %s", running->label);
    ESP_LOGI(TAG, "Partition Address : 0x%08lX", (unsigned long)running->address);
    ESP_LOGI(TAG, "Partition Size    : %lu Bytes", (unsigned long)running->size);

    switch (image_state)
    {
        case ESP_OTA_IMG_NEW:
            ESP_LOGI(TAG, "Image State      : NEW");
            break;

        case ESP_OTA_IMG_PENDING_VERIFY:
            ESP_LOGW(TAG, "Image State      : PENDING VERIFY");
            break;

        case ESP_OTA_IMG_VALID:
            ESP_LOGI(TAG, "Image State      : VALID");
            break;

        case ESP_OTA_IMG_INVALID:
            ESP_LOGE(TAG, "Image State      : INVALID");
            break;

        case ESP_OTA_IMG_ABORTED:
            ESP_LOGE(TAG, "Image State      : ABORTED");
            break;

        case ESP_OTA_IMG_UNDEFINED:
            ESP_LOGW(TAG, "Image State      : UNDEFINED");
            break;

        default:
            ESP_LOGW(TAG, "Image State      : UNKNOWN");
            break;
    }

    ESP_LOGI(TAG, "======================================");
}

/*==============================================================================
 * Check if current image is waiting for verification
 *============================================================================*/

bool rollback_manager_is_pending_verify(void)
{
    const esp_partition_t *running = esp_ota_get_running_partition();

    if (running == NULL)
        return false;

    esp_ota_img_states_t image_state;

    if (esp_ota_get_state_partition(running, &image_state) != ESP_OK)
        return false;

    return (image_state == ESP_OTA_IMG_PENDING_VERIFY);
}

/*==============================================================================
 * Confirm current image
 *============================================================================*/

void rollback_manager_confirm_image(void)
{
    ESP_LOGI(TAG, "Confirming firmware...");

    esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();

    if (err == ESP_OK)
    {
        ESP_LOGI(TAG, "======================================");
        ESP_LOGI(TAG, "Firmware marked as VALID");
        ESP_LOGI(TAG, "Rollback cancelled");
        ESP_LOGI(TAG, "======================================");
    }
    else
    {
        ESP_LOGE(TAG, "Failed to mark image valid : %s", esp_err_to_name(err));
    }
}

/*==============================================================================
 * Force rollback
 *============================================================================*/

void rollback_manager_force_rollback(void)
{
    ESP_LOGE(TAG, "======================================");
    ESP_LOGE(TAG, "Firmware verification FAILED");
    ESP_LOGE(TAG, "Rolling back to previous firmware...");
    ESP_LOGE(TAG, "======================================");

    esp_err_t err = esp_ota_mark_app_invalid_rollback_and_reboot();

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Rollback failed : %s", esp_err_to_name(err));
    }

    while (1)
    {
        /* Should never reach here */
    }
}

/*==============================================================================
 * Handle a failed self-test with persistent blacklisting
 *============================================================================*/

void rollback_manager_handle_failed_image(void)
{
    ESP_LOGE(TAG, "Self-test FAILED handling failure...");

    /* Increment the failure count for this firmware */
    esp_err_t err = nvs_increment_failed_count();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to increment failure count: %s", esp_err_to_name(err));
    }

    uint8_t fail_count = nvs_get_failed_count();
    ESP_LOGI(TAG, "Failure count for this image = %d / %d", fail_count, MAX_FAILED_ATTEMPTS);

    /* If we have reached the limit, blacklist this version */
    if (fail_count >= MAX_FAILED_ATTEMPTS) {
        ESP_LOGW(TAG, "Max attempts reached blacklisting version %s", APP_FW_VERSION);
        err = nvs_save_failed_version(APP_FW_VERSION);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to save failed version: %s", esp_err_to_name(err));
        } else {
            ESP_LOGI(TAG, "Version %s blacklisted in NVS", APP_FW_VERSION);
        }
    }

    /* Now perform the rollback (reboots) */
    rollback_manager_force_rollback();
}