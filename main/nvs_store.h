#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

esp_err_t nvs_store_init(void);

/* WiFi credentials */
esp_err_t nvs_store_set_wifi(const char *ssid, const char *password);
bool      nvs_store_get_wifi(char *ssid, size_t ssid_len, char *password, size_t pass_len);
esp_err_t nvs_store_erase_wifi(void);

/* Wiggle configuration */
esp_err_t nvs_store_set_wiggle(uint32_t interval_s, uint8_t amplitude);
void      nvs_store_get_wiggle(uint32_t *interval_s, uint8_t *amplitude);

/* UDP log target */
esp_err_t nvs_store_set_udp_log(const char *host, uint16_t port);
bool      nvs_store_get_udp_log(char *host, size_t host_len, uint16_t *port);
