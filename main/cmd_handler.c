#include "cmd_handler.h"
#include "ota_update.h"
#include "wifi_prov.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "cmd_handler";

void cmd_handler_on_rx(const uint8_t *data, uint16_t len)
{
    if (len == 0) return;

    switch (data[0]) {
    case CMD_OTA: {
        /* Payload (optional): null-terminated URL string */
        const char *url = (len > 1) ? (const char *)(data + 1) : NULL;
        ESP_LOGI(TAG, "OTA update requested");
        ota_update_start(url);
        break;
    }
    case CMD_WIFI_RESET:
        ESP_LOGI(TAG, "WiFi reset requested");
        wifi_prov_reset();  /* does not return */
        break;
    default:
        ESP_LOGW(TAG, "Unknown command opcode: 0x%02x", data[0]);
        break;
    }
}
