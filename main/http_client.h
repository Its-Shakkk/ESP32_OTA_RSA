#ifndef HTTP_CLIENT_H
#define HTTP_CLIENT_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

void http_create_download_task(void);
esp_err_t http_check_version(void);

#ifdef __cplusplus
}
#endif

#endif