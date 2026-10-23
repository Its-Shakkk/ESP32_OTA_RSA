#ifndef OTA_UPDATE_H
#define OTA_UPDATE_H

#include "manifest.h"
#include "esp_err.h"

esp_err_t ota_download_firmware_with_manifest(const manifest_t *manifest);

#endif