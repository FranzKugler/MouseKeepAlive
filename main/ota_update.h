#pragma once

#include "esp_err.h"

#define OTA_DEFAULT_URL "http://workbench.local:8080/api/firmware/MouseKeepAlive/MouseKeepAlive.bin"

esp_err_t ota_update_start(const char *url);
