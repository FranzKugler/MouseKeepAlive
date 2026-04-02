#include "nvs_store.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "nvs_store";
static const char *NVS_NAMESPACE = "mka_config";

#define WIGGLE_INTERVAL_DEFAULT  300
#define WIGGLE_AMPLITUDE_DEFAULT 5
#define UDP_LOG_PORT_DEFAULT     5555

esp_err_t nvs_store_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS corrupt, erasing...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
    ESP_LOGI(TAG, "NVS initialized");
    return ESP_OK;
}

/* ── WiFi credentials ──────────────────────────────────────────── */

esp_err_t nvs_store_set_wifi(const char *ssid, const char *password)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;

    err = nvs_set_str(h, "wifi_ssid", ssid);
    if (err == ESP_OK) err = nvs_set_str(h, "wifi_pass", password);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}

bool nvs_store_get_wifi(char *ssid, size_t ssid_len, char *password, size_t pass_len)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK) return false;

    esp_err_t err = nvs_get_str(h, "wifi_ssid", ssid, &ssid_len);
    if (err == ESP_OK) err = nvs_get_str(h, "wifi_pass", password, &pass_len);
    nvs_close(h);
    return (err == ESP_OK);
}

esp_err_t nvs_store_erase_wifi(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;

    nvs_erase_key(h, "wifi_ssid");
    nvs_erase_key(h, "wifi_pass");
    err = nvs_commit(h);
    nvs_close(h);
    ESP_LOGI(TAG, "WiFi credentials erased");
    return err;
}

/* ── Wiggle configuration ──────────────────────────────────────── */

esp_err_t nvs_store_set_wiggle(uint32_t interval_s, uint8_t amplitude)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;

    err = nvs_set_u32(h, "wiggle_int", interval_s);
    if (err == ESP_OK) err = nvs_set_u8(h, "wiggle_amp", amplitude);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}

void nvs_store_get_wiggle(uint32_t *interval_s, uint8_t *amplitude)
{
    nvs_handle_t h;
    *interval_s = WIGGLE_INTERVAL_DEFAULT;
    *amplitude  = WIGGLE_AMPLITUDE_DEFAULT;

    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK) return;
    nvs_get_u32(h, "wiggle_int", interval_s);
    nvs_get_u8(h, "wiggle_amp", amplitude);
    nvs_close(h);
}

/* ── UDP log target ────────────────────────────────────────────── */

esp_err_t nvs_store_set_udp_log(const char *host, uint16_t port)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;

    err = nvs_set_str(h, "udp_host", host);
    if (err == ESP_OK) err = nvs_set_u16(h, "udp_port", port);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}

bool nvs_store_get_udp_log(char *host, size_t host_len, uint16_t *port)
{
    nvs_handle_t h;
    *port = UDP_LOG_PORT_DEFAULT;

    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK) return false;

    esp_err_t err = nvs_get_str(h, "udp_host", host, &host_len);
    nvs_get_u16(h, "udp_port", port);
    nvs_close(h);
    return (err == ESP_OK);
}
