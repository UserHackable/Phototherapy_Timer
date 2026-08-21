/*
 * ESP-IDF app: ota_smoke
 *
 * Minimal proof of Wi‑Fi OTA:
 *   1. NVS Wi‑Fi + DHCP
 *   2. DNS phototherapy.ami.lan, else NVS discovery/server_ip if on-subnet
 *   3. uh_ota_check_and_update against http://<host>/firmware/ota_smoke/
 *
 *   ./scripts/fw idf nvs-wifi
 *   ./scripts/fw idf upload ota_smoke
 *   # host a manifest + app.bin (see docs/ota.md)
 *   ./scripts/fw idf monitor ota_smoke
 */

#include <stdio.h>
#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "lwip/inet.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "uh_ota.h"

static const char *TAG = "ota_smoke";

#define NVS_NS_WIFI    "wifi"
#define NVS_KEY_SSID   "ssid"
#define NVS_KEY_PASS   "password"
#define NVS_NS_DISC    "discovery"
#define NVS_KEY_SRV_IP "server_ip"
#define SERVER_DNS_HOST "phototherapy.ami.lan"
#define GOT_IP_BIT     BIT0

static EventGroupHandle_t s_wifi_events;
static char s_server_ip[16];

static void wifi_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGW(TAG, "Wi‑Fi disconnected — reconnect");
        esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *ev = (ip_event_got_ip_t *)data;
        ESP_LOGI(TAG, "DHCP " IPSTR, IP2STR(&ev->ip_info.ip));
        xEventGroupSetBits(s_wifi_events, GOT_IP_BIT);
    }
}

static esp_err_t nvs_load_wifi(char *ssid, size_t ssid_len, char *pass, size_t pass_len)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS_WIFI, NVS_READONLY, &h);
    if (err != ESP_OK) {
        return err;
    }
    size_t len = ssid_len;
    err = nvs_get_str(h, NVS_KEY_SSID, ssid, &len);
    if (err != ESP_OK) {
        nvs_close(h);
        return err;
    }
    len = pass_len;
    err = nvs_get_str(h, NVS_KEY_PASS, pass, &len);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        pass[0] = '\0';
        err = ESP_OK;
    }
    nvs_close(h);
    return err;
}

static void nvs_load_server_ip(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS_DISC, NVS_READONLY, &h) != ESP_OK) {
        return;
    }
    size_t len = sizeof(s_server_ip);
    if (nvs_get_str(h, NVS_KEY_SRV_IP, s_server_ip, &len) != ESP_OK) {
        s_server_ip[0] = '\0';
    } else {
        ESP_LOGI(TAG, "server hint NVS: %s", s_server_ip);
    }
    nvs_close(h);
}

static bool resolve_hostname4(const char *name, uint32_t *addr_nbo)
{
    if (!name || !name[0] || !addr_nbo) {
        return false;
    }
    struct addrinfo hints = {
        .ai_family = AF_INET,
        .ai_socktype = SOCK_STREAM,
    };
    struct addrinfo *res = NULL;
    int err = getaddrinfo(name, NULL, &hints, &res);
    if (err != 0 || !res || !res->ai_addr) {
        ESP_LOGW(TAG, "dns %s failed (%d)", name, err);
        if (res) {
            freeaddrinfo(res);
        }
        return false;
    }
    *addr_nbo = ((struct sockaddr_in *)res->ai_addr)->sin_addr.s_addr;
    char astr[16];
    ip4_addr_t a = { .addr = *addr_nbo };
    ip4addr_ntoa_r(&a, astr, sizeof(astr));
    ESP_LOGI(TAG, "dns %s -> %s", name, astr);
    freeaddrinfo(res);
    return *addr_nbo != 0;
}

static bool wifi_start(void)
{
    char ssid[33] = {0};
    char pass[65] = {0};
    if (nvs_load_wifi(ssid, sizeof(ssid), pass, sizeof(pass)) != ESP_OK) {
        ESP_LOGE(TAG, "no NVS wifi — run ./scripts/fw idf nvs-wifi");
        return false;
    }

    s_wifi_events = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_handler, NULL));

    wifi_config_t wc = {0};
    strncpy((char *)wc.sta.ssid, ssid, sizeof(wc.sta.ssid) - 1);
    strncpy((char *)wc.sta.password, pass, sizeof(wc.sta.password) - 1);
    wc.sta.threshold.authmode = pass[0] ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wc));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(TAG, "Wi‑Fi start SSID='%s'", ssid);
    return true;
}

void app_main(void)
{
    ESP_LOGI(TAG, "ota_smoke starting version=%s", uh_ota_running_version());
    uh_ota_mark_valid();

    esp_err_t nvs = nvs_flash_init();
    if (nvs == ESP_ERR_NVS_NO_FREE_PAGES || nvs == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    } else {
        ESP_ERROR_CHECK(nvs);
    }

    nvs_load_server_ip();
    if (!wifi_start()) {
        return;
    }

    EventBits_t bits = xEventGroupWaitBits(s_wifi_events, GOT_IP_BIT, pdFALSE, pdFALSE,
                                           pdMS_TO_TICKS(30000));
    if (!(bits & GOT_IP_BIT)) {
        ESP_LOGE(TAG, "no DHCP");
        return;
    }

    const char *host = NULL;
    uint32_t dns_addr = 0;
    if (resolve_hostname4(SERVER_DNS_HOST, &dns_addr)) {
        host = SERVER_DNS_HOST;
    } else if (s_server_ip[0] != '\0') {
        host = s_server_ip;
        ESP_LOGW(TAG, "dns %s failed; falling back to NVS IP %s", SERVER_DNS_HOST, host);
    } else {
        ESP_LOGW(TAG, "no %s DNS and no discovery/server_ip in NVS", SERVER_DNS_HOST);
        return;
    }

    /* Stable a few seconds so rollback window can mark valid before OTA work. */
    vTaskDelay(pdMS_TO_TICKS(3000));
    uh_ota_mark_valid();

    char *base = malloc(64);
    if (!base) {
        ESP_LOGE(TAG, "oom");
        return;
    }
    snprintf(base, 64, "http://%s", host);

    uh_ota_config_t *ota_cfg = calloc(1, sizeof(*ota_cfg));
    if (!ota_cfg) {
        free(base);
        return;
    }
    ota_cfg->base_url = base;
    ota_cfg->app_name = "ota_smoke";
    ota_cfg->may_start = NULL;
    ota_cfg->skip_if_same_version = true;

    for (;;) {
        ESP_LOGI(TAG, "OTA check against %s", base);
        esp_err_t err = uh_ota_check_and_update(ota_cfg);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "up to date (or no change)");
        } else {
            ESP_LOGW(TAG, "OTA check: %s", esp_err_to_name(err));
        }
        /* Retry every 60 s while testing publish workflow */
        vTaskDelay(pdMS_TO_TICKS(60000));
    }
}
