#include "ota_manager.h"
#include "nvs_storage.h"
#include <string.h>
#include "app_config.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "mbedtls/sha256.h"
#include "mbedtls/pk.h"
#include "mbedtls/base64.h"

#define OTA_BUFF_SIZE 1024
static const char *TAG = "OTA";

/* --------------------------------------------------------------------------
 * Verify the RSA signature with debug prints
 * -------------------------------------------------------------------------*/
static bool verify_firmware_signature(const uint8_t *hash, size_t hash_len,
                                      const char *signature_b64)
{
    // DEBUG: print first 4 bytes of hash
    ESP_LOGI(TAG, "Hash (first 4 bytes): %02x%02x%02x%02x", hash[0], hash[1], hash[2], hash[3]);
    ESP_LOGI(TAG, "Public key string length: %zu", strlen(OTA_PUBLIC_KEY));

    mbedtls_pk_context pk;
    mbedtls_pk_init(&pk);

    int ret = mbedtls_pk_parse_public_key(&pk, (const unsigned char *)OTA_PUBLIC_KEY,
                                          strlen(OTA_PUBLIC_KEY) + 1);
    if (ret != 0) {
        ESP_LOGE(TAG, "Failed to parse public key: %d", ret);
        mbedtls_pk_free(&pk);
        return false;
    }

    unsigned char sig_buf[512];
    size_t sig_len = 0;
    ret = mbedtls_base64_decode(sig_buf, sizeof(sig_buf), &sig_len,
                                (const unsigned char *)signature_b64, strlen(signature_b64));
    if (ret != 0) {
        ESP_LOGE(TAG, "Failed to decode signature: %d", ret);
        mbedtls_pk_free(&pk);
        return false;
    }
    ESP_LOGI(TAG, "Decoded signature length: %zu bytes (expected 256)", sig_len);

    ret = mbedtls_pk_verify(&pk, MBEDTLS_MD_SHA256, hash, hash_len, sig_buf, sig_len);
    mbedtls_pk_free(&pk);

    if (ret != 0) {
        ESP_LOGE(TAG, "Signature verification FAILED (error %d)", ret);
        return false;
    }

    ESP_LOGI(TAG, "Signature verification PASSED");
    return true;
}

/* --------------------------------------------------------------------------
 * Check partition size
 * -------------------------------------------------------------------------*/
static bool is_partition_sufficient(const esp_partition_t *partition, size_t image_size)
{
    if (image_size > partition->size) {
        ESP_LOGE(TAG, "Image size %zu exceeds partition size %zu", image_size, partition->size);
        return false;
    }
    return true;
}

/* --------------------------------------------------------------------------
 * Main OTA function
 * -------------------------------------------------------------------------*/
esp_err_t ota_download_firmware_with_manifest(const manifest_t *manifest)
{
    const esp_partition_t *update_partition = esp_ota_get_next_update_partition(NULL);
    if (update_partition == NULL) {
        ESP_LOGE(TAG, "No OTA partition found");
        return ESP_ERR_OTA_PARTITION_CONFLICT;
    }

    if (!is_partition_sufficient(update_partition, manifest->size)) {
        return ESP_ERR_INVALID_ARG;
    }

    // Resume stub (always start from 0)
    size_t offset = 0;
    nvs_get_download_offset(&offset);
    if (offset > 0 && offset < manifest->size) {
        ESP_LOGW(TAG, "Resume not implemented starting from 0");
        offset = 0;
    }

    esp_http_client_config_t config = {
        .url = manifest->url,
        .timeout_ms = 30000,
        .cert_pem = (strlen(OTA_SERVER_CERT) ? OTA_SERVER_CERT : NULL),
        .method = HTTP_METHOD_GET,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        ESP_LOGE(TAG, "HTTP client init failed");
        return ESP_ERR_HTTP_CONNECT;
    }

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP open failed: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return err;
    }

    int content_length = esp_http_client_fetch_headers(client);
    if (content_length < 0) {
        ESP_LOGE(TAG, "Failed to get content length");
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return ESP_ERR_HTTP_INVALID_TRANSPORT;
    }

    size_t expected = manifest->size;
    if (content_length != expected) {
        ESP_LOGE(TAG, "Size mismatch: server %d, expected %zu", content_length, expected);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return ESP_ERR_HTTP_INVALID_TRANSPORT;
    }

    ESP_LOGI(TAG, "Downloading %zu bytes (%.2f MB)", expected, (double)expected/(1024*1024));

    esp_ota_handle_t ota_handle;
    err = esp_ota_begin(update_partition, OTA_SIZE_UNKNOWN, &ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin failed: %s", esp_err_to_name(err));
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return err;
    }

    mbedtls_sha256_context sha_ctx;
    mbedtls_sha256_init(&sha_ctx);
    mbedtls_sha256_starts(&sha_ctx, 0);

    char buffer[OTA_BUFF_SIZE];
    size_t total_read = 0;
    int last_pct = -1;

    while (1) {
        int read_len = esp_http_client_read(client, buffer, OTA_BUFF_SIZE);
        if (read_len < 0) {
            ESP_LOGE(TAG, "HTTP read error");
            esp_ota_abort(ota_handle);
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
            return ESP_FAIL;
        }
        if (read_len == 0) break;

        err = esp_ota_write(ota_handle, buffer, read_len);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "OTA write failed: %s", esp_err_to_name(err));
            esp_ota_abort(ota_handle);
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
            return err;
        }

        mbedtls_sha256_update(&sha_ctx, (unsigned char*)buffer, read_len);
        total_read += read_len;

        int pct = (int)((100.0f * total_read) / expected);
        if (pct >= last_pct + 10 || total_read == expected) {
            ESP_LOGI(TAG, "Progress: %d%%", pct);
            last_pct = pct;
        }
    }

    if (total_read != expected) {
        ESP_LOGE(TAG, "Incomplete: expected %zu, got %zu", expected, total_read);
        esp_ota_abort(ota_handle);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return ESP_ERR_HTTP_INVALID_TRANSPORT;
    }

    unsigned char hash[32];
    mbedtls_sha256_finish(&sha_ctx, hash);
    char hex[65];
    for (int i = 0; i < 32; i++) {
        sprintf(&hex[i*2], "%02x", hash[i]);
    }
    hex[64] = '\0';

    if (strcmp(hex, manifest->sha256) != 0) {
        ESP_LOGE(TAG, "SHA256 mismatch! Expected %s, got %s", manifest->sha256, hex);
        esp_ota_abort(ota_handle);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "SHA256 verification OK");
    ESP_LOGI(TAG, "Full SHA256: %s", hex);

    // --- Verify signature (mandatory) ---
    if (manifest->signature[0] == '\0') {
        ESP_LOGE(TAG, "Manifest MISSING signature rejecting (enforced)");
        esp_ota_abort(ota_handle);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return ESP_FAIL;
    }

    if (!verify_firmware_signature(hash, 32, manifest->signature)) {
        ESP_LOGE(TAG, "Firmware signature INVALID aborting OTA");
        esp_ota_abort(ota_handle);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return ESP_FAIL;
    }

    err = esp_ota_end(ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "OTA end failed: %s", esp_err_to_name(err));
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return err;
    }

    err = esp_ota_set_boot_partition(update_partition);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set boot partition: %s", esp_err_to_name(err));
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return err;
    }

    nvs_set_secure_version(manifest->secure_version);

    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    ESP_LOGI(TAG, "OTA completed successfully. Restarting in 3s...");
    vTaskDelay(pdMS_TO_TICKS(3000));
    esp_restart();

    return ESP_OK;
}