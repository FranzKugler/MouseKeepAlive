#pragma once

#include <stdint.h>

/* Workbench BLE command opcodes */
#define CMD_OTA         0x10
#define CMD_WIFI_RESET  0x20

void cmd_handler_on_rx(const uint8_t *data, uint16_t len);
