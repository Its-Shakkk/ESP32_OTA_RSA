#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#include "esp_log.h"

/*-------------------------------------------------------
 * Firmware Information
 *------------------------------------------------------*/
#define APP_FW_VERSION          "1.5.0"
#define APP_DEVICE_NAME         "ESP32_OTA_DEMO"

/*-------------------------------------------------------
 * WiFi Configuration
 *------------------------------------------------------*/
#define WIFI_SSID               "People Tech"
#define WIFI_PASSWORD           "PT!Secure#505@432e"

/*-------------------------------------------------------
 * OTA Server URLs
 *------------------------------------------------------*/
#define OTA_URL                 "http://192.168.16.197:8000/firmware.bin"
#define MANIFEST_URL            "http://192.168.16.197:8000/manifest.json"

/*-------------------------------------------------------
 * LED Configuration
 *------------------------------------------------------*/
#define LED_GPIO                GPIO_NUM_2
#define LED_BLINK_INTERVAL_MS   500

/*-------------------------------------------------------
 * OTA Rollback Policy
 *------------------------------------------------------*/
#define MAX_FAILED_ATTEMPTS     2

/*-------------------------------------------------------
 * OTA Manifest Size
 *------------------------------------------------------*/
#define OTA_MANIFEST_MAX_SIZE   2048

/*-------------------------------------------------------
 * Battery minimum percentage for OTA
 *------------------------------------------------------*/
#define BATTERY_MIN_PERCENT     10

/*-------------------------------------------------------
 * HTTPS Server Certificate (empty = HTTP)
 *------------------------------------------------------*/
#define OTA_SERVER_CERT         ""

/*-------------------------------------------------------
 * Public key for firmware signature verification
 * (Copied exactly from your provided ota_public.pem)
 *------------------------------------------------------*/
#define OTA_PUBLIC_KEY \
    "-----BEGIN PUBLIC KEY-----\n" \
    "MIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEA1K/j4Hxm7Meh1tHbeM0J\n" \
    "6Pk+dignSLpMnuh+eIuhc/3tp38Zlegcw1FJcmlxUz7XLzoym2fxpKYFd+DCZDo1\n" \
    "dtOyHjVXn5BNXBHpLGqlS+uincnCfU2kziC5hDAWfoY9iuQ0hPZpkOGphfRc8Z79\n" \
    "SOH6d2hqsYTu/tWF0IhGU2ikQf46T4StSG6FRTLMjuSAP/XGi7CjCcVY76/Hyzmo\n" \
    "pO+YJK4AqRyXOY2+pQ+ClD5DcfxbNlLOjRIRlpVR50lcy7f7/WM5VBS3bCmGVpNS\n" \
    "I0+7EeTKwnIqVJBY5gbqyMYKkpfx82YAHxPHjjqYx4efZczhlOo5wf/v3HnZ+c14\n" \
    "HwIDAQAB\n" \
    "-----END PUBLIC KEY-----\n"


#endif