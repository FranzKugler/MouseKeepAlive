#include "esp_log.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "driver/gpio.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_store.h"
#include "udp_log.h"
#include "wifi_prov.h"
#include "ble_nus.h"
#include "cmd_handler.h"
#include "http_server.h"

static const char *TAG = "app_main";

#define FW_VERSION    "0.1.0"
#define BOOT_CNT_KEY  "boot_count"
#define FACTORY_RESET_GPIO  0          /* BOOT button (GPIO0) */
#define FACTORY_RESET_HOLD_MS  5000

/* ── Boot counter ──────────────────────────────────────────────── */

static uint32_t read_increment_boot_count(void)
{
    nvs_handle_t h;
    uint32_t cnt = 0;
    if (nvs_open("mka_sys", NVS_READWRITE, &h) == ESP_OK) {
        nvs_get_u32(h, BOOT_CNT_KEY, &cnt);
        cnt++;
        nvs_set_u32(h, BOOT_CNT_KEY, cnt);
        nvs_commit(h);
        nvs_close(h);
    }
    return cnt;
}

/* ── IP event → start HTTP server ──────────────────────────────── */

static void on_sta_got_ip(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    http_server_start();
}

/* ── Heartbeat task ────────────────────────────────────────────── */

static void alive_task(void *arg)
{
    uint32_t tick = 0;
    while (1) {
        ESP_LOGI(TAG, "alive %lu", (unsigned long)tick++);
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}

/* ── Factory reset via BOOT button ────────────────────────────── */

static void factory_reset_task(void *arg)
{
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << FACTORY_RESET_GPIO),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
    };
    gpio_config(&io);

    while (1) {
        if (gpio_get_level(FACTORY_RESET_GPIO) == 0) {
            vTaskDelay(pdMS_TO_TICKS(FACTORY_RESET_HOLD_MS));
            if (gpio_get_level(FACTORY_RESET_GPIO) == 0) {
                ESP_LOGW(TAG, "BOOT button held 5s — factory reset triggered");
                wifi_prov_reset();
            }
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

/* ── app_main ──────────────────────────────────────────────────── */

void app_main(void)
{
    ESP_LOGI(TAG, "=== MouseKeepAlive v%s ===", FW_VERSION);

    /* 1. NVS — must be first; other modules depend on it */
    nvs_store_init();

    /* 2. Boot count */
    uint32_t boot_count = read_increment_boot_count();
    http_server_set_boot_count(boot_count);
    http_server_set_start_us(esp_timer_get_time());
    ESP_LOGI(TAG, "Boot count: %"PRIu32, boot_count);

    /* 3. Network stack — must be up before UDP logging or WiFi events fire */
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* 4. UDP debug logging — active from here onwards */
    char udp_host[64] = "workbench.local";
    uint16_t udp_port = 5555;
    nvs_store_get_udp_log(udp_host, sizeof(udp_host), &udp_port);
    udp_log_init(udp_host, udp_port);

    /* 5. Register IP event → HTTP server start (STA mode only) */
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_sta_got_ip, NULL));

    /* 6. WiFi — STA (stored creds) or AP (captive portal) */
    wifi_prov_init();

    /* 7. In STA mode: wait for WiFi before BLE to avoid coexistence conflicts during association */
    if (!wifi_prov_is_ap_mode()) {
        ESP_LOGI(TAG, "Waiting for WiFi...");
        for (int i = 0; i < 150 && !wifi_prov_is_connected(); i++)
            vTaskDelay(pdMS_TO_TICKS(100));
    }

    /* 8. BLE — NUS for workbench command channel */
    ble_nus_init(cmd_handler_on_rx);

    /* 9. In AP mode: HTTP server starts immediately (no IP event fires) */
    if (wifi_prov_is_ap_mode()) {
        http_server_start();
    }

    /* 10. Factory reset monitor */
    xTaskCreate(factory_reset_task, "factory_rst", 2048, NULL, 2, NULL);

    /* 11. Heartbeat */
    xTaskCreate(alive_task, "alive", 2048, NULL, 1, NULL);

    ESP_LOGI(TAG, "Init complete, running event-driven");
}
