#include "ota_update.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "ota_update";

/* URL is passed via task argument (heap-allocated, freed after use) */
static void ota_task(void *arg)
{
    char *url = (char *)arg;

    ESP_LOGI(TAG, "Starting OTA from %s", url);

    esp_http_client_config_t http_cfg = {
        .url              = url,
        .keep_alive_enable = true,
    };

    esp_https_ota_config_t ota_cfg = {
        .http_config = &http_cfg,
    };

    esp_err_t ret = esp_https_ota(&ota_cfg);
    free(url);

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "OTA succeeded, rebooting...");
        esp_restart();
    } else {
        ESP_LOGE(TAG, "OTA failed: %s", esp_err_to_name(ret));
    }

    vTaskDelete(NULL);
}

esp_err_t ota_update_start(const char *url)
{
    const char *effective_url = (url && strlen(url) > 0) ? url : OTA_DEFAULT_URL;

    char *url_copy = strdup(effective_url);
    if (!url_copy) return ESP_ERR_NO_MEM;

    BaseType_t ret = xTaskCreate(ota_task, "ota_task", 8192, url_copy, 5, NULL);
    if (ret != pdPASS) {
        free(url_copy);
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
