#include "nvs_storage.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

#define OTA_NAMESPACE      "ota"

#define KEY_FAILED_VERSION "fail_ver"
#define KEY_FAILED_COUNT   "fail_cnt"
#define KEY_COUNT_VERSION  "cnt_ver"

static const char *TAG = "NVS";

static esp_err_t open_namespace(nvs_handle_t *handle)
{
    return nvs_open(OTA_NAMESPACE, NVS_READWRITE, handle);
}

esp_err_t nvs_storage_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "NVS Initialized");
    }
    return err;
}

esp_err_t nvs_save_failed_version(const char *version)
{
    nvs_handle_t handle;
    esp_err_t err = open_namespace(&handle);
    if (err != ESP_OK) return err;
    err = nvs_set_str(handle, KEY_FAILED_VERSION, version);
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    return err;
}

esp_err_t nvs_get_failed_version(char *version, size_t len)
{
    nvs_handle_t handle;
    esp_err_t err = open_namespace(&handle);
    if (err != ESP_OK) return err;
    err = nvs_get_str(handle, KEY_FAILED_VERSION, version, &len);
    nvs_close(handle);
    return err;
}

esp_err_t nvs_clear_failed_version(void)
{
    nvs_handle_t handle;
    esp_err_t err = open_namespace(&handle);
    if (err != ESP_OK) return err;
    err = nvs_erase_key(handle, KEY_FAILED_VERSION);
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    return err;
}

esp_err_t nvs_set_failed_count(uint8_t count)
{
    nvs_handle_t handle;
    esp_err_t err = open_namespace(&handle);
    if (err != ESP_OK) return err;
    err = nvs_set_u8(handle, KEY_FAILED_COUNT, count);
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    return err;
}

uint8_t nvs_get_failed_count(void)
{
    uint8_t count = 0;
    nvs_handle_t handle;
    if (open_namespace(&handle) != ESP_OK) return 0;
    nvs_get_u8(handle, KEY_FAILED_COUNT, &count);
    nvs_close(handle);
    return count;
}

esp_err_t nvs_increment_failed_count(void)
{
    uint8_t count = nvs_get_failed_count();
    count++;
    return nvs_set_failed_count(count);
}

esp_err_t nvs_reset_failed_count(void)
{
    return nvs_set_failed_count(0);
}

esp_err_t nvs_set_count_version(const char *version)
{
    nvs_handle_t handle;
    esp_err_t err = open_namespace(&handle);
    if (err != ESP_OK) return err;
    err = nvs_set_str(handle, KEY_COUNT_VERSION, version);
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    return err;
}

esp_err_t nvs_get_count_version(char *version, size_t len)
{
    nvs_handle_t handle;
    esp_err_t err = open_namespace(&handle);
    if (err != ESP_OK) return err;
    err = nvs_get_str(handle, KEY_COUNT_VERSION, version, &len);
    nvs_close(handle);
    return err;
}

esp_err_t nvs_set_secure_version(uint32_t version)
{
    nvs_handle_t handle;
    esp_err_t err = open_namespace(&handle);
    if (err != ESP_OK) return err;
    err = nvs_set_u32(handle, "secure_ver", version);
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    return err;
}

uint32_t nvs_get_secure_version(void)
{
    nvs_handle_t handle;
    uint32_t ver = 0;
    if (open_namespace(&handle) != ESP_OK) return 0;
    nvs_get_u32(handle, "secure_ver", &ver);
    nvs_close(handle);
    return ver;
}

esp_err_t nvs_set_download_offset(size_t offset)
{
    nvs_handle_t handle;
    esp_err_t err = open_namespace(&handle);
    if (err != ESP_OK) return err;
    err = nvs_set_u32(handle, "dl_offset", (uint32_t)offset);
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    return err;
}

esp_err_t nvs_get_download_offset(size_t *offset)
{
    nvs_handle_t handle;
    uint32_t val = 0;
    if (open_namespace(&handle) != ESP_OK) return ESP_FAIL;
    esp_err_t err = nvs_get_u32(handle, "dl_offset", &val);
    nvs_close(handle);
    if (err == ESP_OK) *offset = val;
    return err;
}

void nvs_print_ota_info(void)
{
    char version[32] = {0};
    if (nvs_get_failed_version(version, sizeof(version)) != ESP_OK) {
        strcpy(version, "NONE");
    }
    uint32_t secure_ver = nvs_get_secure_version();
    ESP_LOGI(TAG, "===============================");
    ESP_LOGI(TAG, "Failed Version : %s", version);
    ESP_LOGI(TAG, "Failed Count   : %d", nvs_get_failed_count());
    ESP_LOGI(TAG, "Secure Version : %lu", secure_ver);
    ESP_LOGI(TAG, "===============================");
}