#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

typedef void (*ble_nus_rx_cb_t)(const uint8_t *data, uint16_t len);

#if CONFIG_BT_ENABLED
esp_err_t ble_nus_init(ble_nus_rx_cb_t rx_cb);
bool      ble_nus_is_connected(void);
#else
static inline esp_err_t ble_nus_init(ble_nus_rx_cb_t rx_cb) { (void)rx_cb; return ESP_OK; }
static inline bool ble_nus_is_connected(void) { return false; }
#endif
