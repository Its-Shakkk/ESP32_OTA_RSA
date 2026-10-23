#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "http_client.h"
#include "app_config.h"
#include "ota_manager.h"
#include "nvs_storage.h"
#include "manifest.h"
#include <string.h>
#include <stdlib.h>
#include "esp_http_client.h"
#include "esp_log.h"
#include "cJSON.h"

static const char *TAG = "HTTP";

/* --------------------------------------------------------------------------
 * Semantic version comparator
 */
static int compare_version(const char *v1, const char *v2)
{
    int maj1, min1, pat1, maj2, min2, pat2;
    if (sscanf(v1, "%d.%d.%d", &maj1, &min1, &pat1) != 3) return -2;
    if (sscanf(v2, "%d.%d.%d", &maj2, &min2, &pat2) != 3) return -2;

    if (maj1 != maj2) return (maj1 > maj2) ? 1 : -1;
    if (min1 != min2) return (min1 > min2) ? 1 : -1;
    if (pat1 != pat2) return (pat1 > pat2) ? 1 : -1;
    return 0;
}

/* --------------------------------------------------------------------------
 * HTTP event handler
 */
typedef struct {
    char *buffer;
    size_t size;
    size_t used;
} http_response_t;

static esp_err_t manifest_http_event_handler(esp_http_client_event_t *evt)
{
    http_response_t *resp = (http_response_t *)evt->user_data;
    switch (evt->event_id) {
        case HTTP_EVENT_ON_DATA:
            if (resp->used + evt->data_len < resp->size) {
                memcpy(resp->buffer + resp->used, evt->data, evt->data_len);
                resp->used += evt->data_len;
            } else {
                ESP_LOGE(TAG, "Buffer overflow! Needed %zu bytes", resp->used + evt->data_len);
                return ESP_ERR_NO_MEM;
            }
            break;
        default:
            break;
    }
    return ESP_OK;
}

/* --------------------------------------------------------------------------
 * Download manifest with retries
 */
static esp_err_t download_manifest(manifest_t *out)
{
    int attempts = 0;
    int max_attempts = 3;
    int base_delay_ms = 1000;

    while (attempts < max_attempts) {
        attempts++;
        ESP_LOGI(TAG, "Downloading manifest, attempt %d/%d", attempts, max_attempts);

        char buffer[OTA_MANIFEST_MAX_SIZE];
        http_response_t resp = {
            .buffer = buffer,
            .size = sizeof(buffer),
            .used = 0,
        };

        esp_http_client_config_t config = {
            .url = MANIFEST_URL,
            .timeout_ms = 5000,
            .event_handler = manifest_http_event_handler,
            .user_data = &resp,
            .method = HTTP_METHOD_GET,
            .cert_pem = (strlen(OTA_SERVER_CERT) ? OTA_SERVER_CERT : NULL),
        };

        esp_http_client_handle_t client = esp_http_client_init(&config);
        if (!client) {
            ESP_LOGE(TAG, "HTTP client init failed");
            goto retry;
        }

        esp_err_t err = esp_http_client_perform(client);
        int status = esp_http_client_get_status_code(client);
        esp_http_client_cleanup(client);

        if (err != ESP_OK) {
            ESP_LOGE(TAG, "HTTP perform failed: %s", esp_err_to_name(err));
            goto retry;
        }

        if (status != 200) {
            ESP_LOGE(TAG, "HTTP status %d", status);
            goto retry;
        }

        if (resp.used == 0) {
            ESP_LOGE(TAG, "Empty response");
            goto retry;
        }

        buffer[resp.used] = '\0';

        cJSON *root = cJSON_Parse(buffer);
        if (!root) {
            ESP_LOGE(TAG, "JSON parse failed");
            goto retry;
        }

        bool ok = true;
        cJSON *item;

        item = cJSON_GetObjectItem(root, "version");
        if (cJSON_IsString(item)) {
            strlcpy(out->version, item->valuestring, sizeof(out->version));
        } else ok = false;

        item = cJSON_GetObjectItem(root, "url");
        if (cJSON_IsString(item)) {
            strlcpy(out->url, item->valuestring, sizeof(out->url));
        } else ok = false;

        item = cJSON_GetObjectItem(root, "size");
        if (cJSON_IsNumber(item)) {
            out->size = (size_t)item->valuedouble;
        } else ok = false;

        item = cJSON_GetObjectItem(root, "sha256");
        if (cJSON_IsString(item) && strlen(item->valuestring) == 64) {
            strlcpy(out->sha256, item->valuestring, sizeof(out->sha256));
        } else ok = false;

        item = cJSON_GetObjectItem(root, "secure_version");
        out->secure_version = (cJSON_IsNumber(item)) ? (uint32_t)item->valuedouble : 0;

        item = cJSON_GetObjectItem(root, "hw_rev");
        if (cJSON_IsString(item)) strlcpy(out->hw_rev, item->valuestring, sizeof(out->hw_rev));
        else strcpy(out->hw_rev, "*");

        item = cJSON_GetObjectItem(root, "min_bootloader");
        if (cJSON_IsString(item)) strlcpy(out->min_bootloader, item->valuestring, sizeof(out->min_bootloader));
        else strcpy(out->min_bootloader, "1.0.0");

        item = cJSON_GetObjectItem(root, "mandatory");
        out->mandatory = cJSON_IsTrue(item) ? true : false;

        item = cJSON_GetObjectItem(root, "project");
        if (cJSON_IsString(item)) strlcpy(out->project, item->valuestring, sizeof(out->project));
        else strcpy(out->project, "");

        item = cJSON_GetObjectItem(root, "board");
        if (cJSON_IsString(item)) strlcpy(out->board, item->valuestring, sizeof(out->board));
        else strcpy(out->board, "");

        // --- NEW: parse signature ---
        item = cJSON_GetObjectItem(root, "signature");
        if (cJSON_IsString(item)) {
            strlcpy(out->signature, item->valuestring, sizeof(out->signature));
        } else {
            out->signature[0] = '\0';   // empty if not present (backward compatibility)
        }

        cJSON_Delete(root);

        if (!ok) {
            ESP_LOGE(TAG, "Manifest missing required field(s)");
            goto retry;
        }

        ESP_LOGI(TAG, "Manifest downloaded successfully");
        return ESP_OK;

retry:
        if (attempts < max_attempts) {
            int delay_ms = base_delay_ms * (1 << (attempts - 1));
            ESP_LOGI(TAG, "Retrying in %d ms", delay_ms);
            vTaskDelay(pdMS_TO_TICKS(delay_ms));
        }
    }
    ESP_LOGE(TAG, "Manifest download failed after %d attempts", max_attempts);
    return ESP_FAIL;
}

/* --------------------------------------------------------------------------
 * Public: check for new firmware version
 */
esp_err_t http_check_version(void)
{
    manifest_t manifest;
    if (download_manifest(&manifest) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get manifest");
        return ESP_FAIL;
    }

    int cmp = compare_version(manifest.version, APP_FW_VERSION);
    if (cmp < 0) {
        ESP_LOGW(TAG, "Latest version (%s) is older – skipping", manifest.version);
        return ESP_OK;
    }
    if (cmp == 0) {
        ESP_LOGI(TAG, "Firmware is up to date.");
        return ESP_OK;
    }

    // Blacklist check
    char failed_ver[32] = {0};
    if (nvs_get_failed_version(failed_ver, sizeof(failed_ver)) == ESP_OK &&
        strcmp(manifest.version, failed_ver) == 0) {
        ESP_LOGW(TAG, "Version %s is BLACKLISTED, skipping.", manifest.version);
        return ESP_OK;
    }

    // Anti‑rollback: secure_version
    uint32_t current_secure = nvs_get_secure_version();
    if (manifest.secure_version < current_secure) {
        ESP_LOGE(TAG, "Secure version %lu < current %lu – rejecting", 
                 manifest.secure_version, current_secure);
        return ESP_FAIL;
    }

    // Reset failure counter if new version
    char stored_ver[32] = {0};
    esp_err_t err = nvs_get_count_version(stored_ver, sizeof(stored_ver));
    if (err != ESP_OK || strcmp(stored_ver, manifest.version) != 0) {
        nvs_reset_failed_count();
        nvs_set_count_version(manifest.version);
        ESP_LOGI(TAG, "New OTA version resetting failure counter.");
    } else {
        ESP_LOGI(TAG, "Retrying version failure counter = %d", nvs_get_failed_count());
    }

    ESP_LOGW(TAG, "******** UPDATE AVAILABLE ********");
    ESP_LOGW(TAG, "Current : %s", APP_FW_VERSION);
    ESP_LOGW(TAG, "Latest  : %s", manifest.version);
    ESP_LOGW(TAG, "Secure  : %lu (new) vs %lu (current)", manifest.secure_version, current_secure);
    ESP_LOGW(TAG, "**********************************");
    ESP_LOGI(TAG, "Starting OTA in 3 seconds...");
    // esp_task_wdt_reset();   // feed watchdog before delay
    // vTaskDelay(pdMS_TO_TICKS(3000));

    err = ota_download_firmware_with_manifest(&manifest);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "OTA download failed: %s", esp_err_to_name(err));
        return err;
    }

    return ESP_OK;
}