#include "http_server.h"
#include "wifi_prov.h"
#include "ble_nus.h"
#include "ota_update.h"
#include "nvs_store.h"
#include "esp_http_server.h"
#include "esp_ota_ops.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "cJSON.h"
#include <string.h>

static const char *TAG = "http_srv";
static uint32_t s_boot_count = 0;
static int64_t  s_start_us = 0;

/* Wiggle config is read/written live via NVS — no in-memory cache needed here */

void http_server_set_boot_count(uint32_t count) { s_boot_count = count; }
void http_server_set_start_us(int64_t us)        { s_start_us = us; }

/* ── GET /status ───────────────────────────────────────────────── */

static esp_err_t status_handler(httpd_req_t *req)
{
    const esp_app_desc_t *app = esp_app_get_description();

    uint32_t wiggle_interval = 300;
    uint8_t  wiggle_amplitude = 5;
    nvs_store_get_wiggle(&wiggle_interval, &wiggle_amplitude);

    int64_t now_us = esp_timer_get_time();
    uint32_t uptime_s = (uint32_t)((now_us - s_start_us) / 1000000LL);

    wifi_ap_record_t ap;
    int8_t rssi = 0;
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) rssi = ap.rssi;

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "firmware",           app->version);
    cJSON_AddNumberToObject(root, "uptime_s",           uptime_s);
    cJSON_AddNumberToObject(root, "boot_count",         s_boot_count);
    cJSON_AddNumberToObject(root, "wifi_rssi",          rssi);
    cJSON_AddBoolToObject  (root, "wifi_connected",     wifi_prov_is_connected());
    cJSON_AddBoolToObject  (root, "ble_connected",      ble_nus_is_connected());
    cJSON_AddNumberToObject(root, "wiggle_interval_s",  wiggle_interval);
    cJSON_AddNumberToObject(root, "wiggle_amplitude",   wiggle_amplitude);

    char *json = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json);
    cJSON_free(json);
    cJSON_Delete(root);
    return ESP_OK;
}

/* ── POST /config ──────────────────────────────────────────────── */

static esp_err_t config_handler(httpd_req_t *req)
{
    char buf[256];
    int len = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (len <= 0) { httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No body"); return ESP_FAIL; }
    buf[len] = '\0';

    cJSON *json = cJSON_Parse(buf);
    if (!json) { httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON"); return ESP_FAIL; }

    uint32_t interval  = 300;
    uint8_t  amplitude = 5;
    nvs_store_get_wiggle(&interval, &amplitude);

    cJSON *j;
    if ((j = cJSON_GetObjectItem(json, "wiggle_interval_s")) && cJSON_IsNumber(j))
        interval = (uint32_t)j->valuedouble;
    if ((j = cJSON_GetObjectItem(json, "wiggle_amplitude")) && cJSON_IsNumber(j))
        amplitude = (uint8_t)j->valuedouble;

    nvs_store_set_wiggle(interval, amplitude);

    /* Optional: update UDP log target */
    const char *host = cJSON_GetStringValue(cJSON_GetObjectItem(json, "udp_log_host"));
    if ((j = cJSON_GetObjectItem(json, "udp_log_port")) && cJSON_IsNumber(j) && host)
        nvs_store_set_udp_log(host, (uint16_t)j->valuedouble);

    cJSON_Delete(json);
    ESP_LOGI(TAG, "Config updated: interval=%"PRIu32"s amplitude=%d", interval, amplitude);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"status\":\"ok\"}");
    return ESP_OK;
}

/* ── POST /ota/trigger ─────────────────────────────────────────── */

static esp_err_t ota_trigger_handler(httpd_req_t *req)
{
    char buf[256];
    int len = httpd_req_recv(req, buf, sizeof(buf) - 1);
    char url[200] = {0};

    if (len > 0) {
        buf[len] = '\0';
        cJSON *json = cJSON_Parse(buf);
        if (json) {
            const char *u = cJSON_GetStringValue(cJSON_GetObjectItem(json, "url"));
            if (u) strncpy(url, u, sizeof(url) - 1);
            cJSON_Delete(json);
        }
    }

    ESP_LOGI(TAG, "OTA update requested via HTTP");
    esp_err_t err = ota_update_start(strlen(url) ? url : NULL);

    httpd_resp_set_type(req, "application/json");
    if (err == ESP_OK)
        httpd_resp_sendstr(req, "{\"status\":\"ok\",\"message\":\"OTA started\"}");
    else
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to start OTA");
    return ESP_OK;
}

/* ── POST /config/reset ────────────────────────────────────────── */

static esp_err_t config_reset_handler(httpd_req_t *req)
{
    ESP_LOGW(TAG, "Factory reset requested via HTTP");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"status\":\"ok\",\"message\":\"Resetting...\"}");
    wifi_prov_reset();  /* erases NVS and reboots */
    return ESP_OK;
}

/* ── Server start ──────────────────────────────────────────────── */

esp_err_t http_server_start(void)
{
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.ctrl_port   = 32769;
    config.uri_match_fn = httpd_uri_match_wildcard;

    esp_err_t err = httpd_start(&server, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server: %s", esp_err_to_name(err));
        return err;
    }

    static const httpd_uri_t status_get = {
        .uri = "/status", .method = HTTP_GET, .handler = status_handler
    };
    static const httpd_uri_t config_post = {
        .uri = "/config", .method = HTTP_POST, .handler = config_handler
    };
    static const httpd_uri_t ota_post = {
        .uri = "/ota/trigger", .method = HTTP_POST, .handler = ota_trigger_handler
    };
    static const httpd_uri_t reset_post = {
        .uri = "/config/reset", .method = HTTP_POST, .handler = config_reset_handler
    };

    httpd_register_uri_handler(server, &status_get);
    httpd_register_uri_handler(server, &config_post);
    httpd_register_uri_handler(server, &ota_post);
    httpd_register_uri_handler(server, &reset_post);

    ESP_LOGI(TAG, "HTTP server started (GET /status, POST /config, POST /ota/trigger, POST /config/reset)");
    return ESP_OK;
}
