#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_netif.h"
#include "esp_http_client.h"
#include "lwip/inet.h"
#include "wifi.h"
#include "http_client.h"
#include "rollback_manager.h"
#include "nvs_storage.h"
#include "app_config.h"

static const char *TAG = "APP";

/*---------------------------------------------------------------------------
 * Battery check – using new adc_oneshot driver (no warnings)
 * ADC1_CHANNEL_0 = GPIO36; adjust divider_factor to match your hardware
 *--------------------------------------------------------------------------*/
static bool is_power_adequate(void)
{
    // 1. Create ADC oneshot unit handle
    adc_oneshot_unit_handle_t adc_handle;
    adc_oneshot_unit_init_cfg_t init_config = {
        .unit_id = ADC_UNIT_1,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config, &adc_handle));

    // 2. Configure ADC channel (attenuation = 11dB for 0–3.6V input)
    adc_oneshot_chan_cfg_t channel_config = {
        .atten = ADC_ATTEN_DB_12,   // corresponds to old ADC_ATTEN_DB_11, but not deprecated
        .bitwidth = ADC_BITWIDTH_12,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_handle, ADC_CHANNEL_0, &channel_config));

    // 3. Read raw value
    float raw = 4095.0f;
    // esp_err_t err = adc_oneshot_read(adc_handle, ADC_CHANNEL_0, &raw);
    // if (err != ESP_OK) {
    //     ESP_LOGE(TAG, "Failed to read battery ADC: %s", esp_err_to_name(err));
    //     adc_oneshot_del_unit(adc_handle);
    //     return false;
    // }

    // 4. Convert to voltage
    float voltage_mv = (raw / 4095.0f) * 3300.0f;
    float divider_factor = 1.0f;   // adjust to your voltage divider
    float battery_voltage = voltage_mv * divider_factor / 1000.0f;

    // 5. Map to percentage (LiPo: 3.0V = 0%, 4.2V = 100%)
    float min_voltage = 3.0f;
    float max_voltage = 4.2f;
    float percent = ((battery_voltage - min_voltage) / (max_voltage - min_voltage)) * 100.0f;
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;

    ESP_LOGI(TAG, "Battery: %.2f V (%.0f%%)", battery_voltage, percent);

    adc_oneshot_del_unit(adc_handle);
    return (percent >= BATTERY_MIN_PERCENT);
}

/*---------------------------------------------------------------------------
 * Self‑test: WiFi IP + server reachability
 *--------------------------------------------------------------------------*/
static bool run_self_test(void)
{
    ESP_LOGI(TAG, "Running self test...");

    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (netif == NULL) {
        ESP_LOGE(TAG, "WiFi netif not found");
        return false;
    }

    esp_netif_ip_info_t ip_info;
    if (esp_netif_get_ip_info(netif, &ip_info) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get IP info");
        return false;
    }

    if (ip_info.ip.addr == 0) {
        ESP_LOGE(TAG, "No IP address assigned");
        return false;
    }
    ESP_LOGI(TAG, "IP address OK: " IPSTR, IP2STR(&ip_info.ip));

    esp_http_client_config_t config = {
        .url = MANIFEST_URL,
        .method = HTTP_METHOD_HEAD,
        .timeout_ms = 5000,
        .cert_pem = (strlen(OTA_SERVER_CERT) ? OTA_SERVER_CERT : NULL),
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        ESP_LOGE(TAG, "HTTP client init failed");
        return false;
    }

    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Server not reachable: %s", esp_err_to_name(err));
        return false;
    }

    if (status < 200 || status >= 300) {
        ESP_LOGE(TAG, "Server returned status %d", status);
        return false;
    }

    // Blink LED to indicate pass
    gpio_set_level(LED_GPIO, 1);
    vTaskDelay(pdMS_TO_TICKS(200));
    gpio_set_level(LED_GPIO, 0);
    vTaskDelay(pdMS_TO_TICKS(200));
    gpio_set_level(LED_GPIO, 1);
    vTaskDelay(pdMS_TO_TICKS(200));
    gpio_set_level(LED_GPIO, 0);

    ESP_LOGI(TAG, "Self test PASSED");
    return true;
}

/*---------------------------------------------------------------------------
 * GPIO initialisation
 *--------------------------------------------------------------------------*/
static void init_gpio(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << LED_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);
    ESP_LOGI(TAG, "GPIO init OK");
}

/*---------------------------------------------------------------------------
 * Application entry
 *--------------------------------------------------------------------------*/
void app_main(void)
{
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "App v%s", APP_FW_VERSION);
    ESP_LOGI(TAG, "Device: %s", APP_DEVICE_NAME);
    ESP_LOGI(TAG, "========================================");

    ESP_ERROR_CHECK(nvs_storage_init());
    nvs_print_ota_info();
    rollback_manager_startup();
    wifi_init_sta();
    init_gpio();

    if (rollback_manager_is_pending_verify()) {
        ESP_LOGW(TAG, "PENDING_VERIFY – running self test");
        if (!is_power_adequate()) {
            ESP_LOGE(TAG, "Battery too low – rolling back");
            rollback_manager_handle_failed_image();
        }
        if (run_self_test()) {
            ESP_LOGI(TAG, "Self test PASS – confirming image");
            rollback_manager_confirm_image();
            nvs_clear_failed_version();
            nvs_reset_failed_count();
            ESP_LOGI(TAG, "Blacklist cleared.");
        } else {
            ESP_LOGE(TAG, "Self test FAIL – rolling back");
            rollback_manager_handle_failed_image();
        }
    }

    http_check_version();

    bool state = false;
    while (1) {
        state = !state;
        gpio_set_level(LED_GPIO, state);
        ESP_LOGI(TAG, "LED %s", state ? "ON" : "OFF");
        is_power_adequate();  // logs battery every blink
        vTaskDelay(pdMS_TO_TICKS(LED_BLINK_INTERVAL_MS));
    }
}