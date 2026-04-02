#pragma once

#include "esp_err.h"
#include <stdint.h>

esp_err_t http_server_start(void);
void      http_server_set_boot_count(uint32_t count);
void      http_server_set_start_us(int64_t us);
