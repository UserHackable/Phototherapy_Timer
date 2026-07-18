/*
 * ESP-IDF app: wifi_scan
 *
 * Bring up Wi-Fi in station mode, scan for access points, print a table
 * over UART (115200). Does not join any network. SSR / lamp path unused.
 */

#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "wifi_scan";

/* How long to wait for a scan to finish (ms). */
#define SCAN_TIMEOUT_MS 10000

/* Max APs to fetch (driver may return fewer). */
#define MAX_AP_COUNT 32

static const char *authmode_str(wifi_auth_mode_t m)
{
    switch (m) {
    case WIFI_AUTH_OPEN:            return "OPEN";
    case WIFI_AUTH_WEP:             return "WEP";
    case WIFI_AUTH_WPA_PSK:         return "WPA";
    case WIFI_AUTH_WPA2_PSK:        return "WPA2";
    case WIFI_AUTH_WPA_WPA2_PSK:    return "WPA/WPA2";
    case WIFI_AUTH_WPA2_ENTERPRISE: return "WPA2-ENT";
    case WIFI_AUTH_WPA3_PSK:        return "WPA3";
    case WIFI_AUTH_WPA2_WPA3_PSK:   return "WPA2/WPA3";
    default:                        return "?";
    }
}

static void wifi_init_for_scan(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
}

static void do_scan_and_print(void)
{
    wifi_scan_config_t scan_cfg = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = true,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time.active.min = 100,
        .scan_time.active.max = 300,
    };

    ESP_LOGI(TAG, "Scanning…");
    esp_err_t err = esp_wifi_scan_start(&scan_cfg, true /* block */);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "scan_start failed: %s", esp_err_to_name(err));
        return;
    }

    uint16_t ap_count = 0;
    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_num(&ap_count));

    uint16_t fetch = ap_count > MAX_AP_COUNT ? MAX_AP_COUNT : ap_count;
    wifi_ap_record_t *aps = calloc(fetch, sizeof(wifi_ap_record_t));
    if (!aps) {
        ESP_LOGE(TAG, "out of memory for %u APs", fetch);
        return;
    }

    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&fetch, aps));

    printf("\n");
    printf("Found %u AP(s) (showing %u):\n", ap_count, fetch);
    printf("%-3s %-32s %-4s %-5s %-10s %s\n",
           "#", "SSID", "CH", "RSSI", "AUTH", "BSSID");
    printf("--- -------------------------------- ---- ----- ---------- -----------------\n");

    for (uint16_t i = 0; i < fetch; i++) {
        const wifi_ap_record_t *ap = &aps[i];
        char ssid[33];
        memcpy(ssid, ap->ssid, 32);
        ssid[32] = '\0';
        if (ssid[0] == '\0') {
            snprintf(ssid, sizeof(ssid), "(hidden)");
        }

        printf("%-3u %-32s %-4u %-5d %-10s %02x:%02x:%02x:%02x:%02x:%02x\n",
               i + 1,
               ssid,
               ap->primary,
               ap->rssi,
               authmode_str(ap->authmode),
               ap->bssid[0], ap->bssid[1], ap->bssid[2],
               ap->bssid[3], ap->bssid[4], ap->bssid[5]);
    }
    printf("\n");

    free(aps);
}

void app_main(void)
{
    ESP_LOGI(TAG, "wifi_scan starting");

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    } else {
        ESP_ERROR_CHECK(ret);
    }

    wifi_init_for_scan();

    /* Scan once at boot, then every 30 s so monitor stays useful. */
    while (1) {
        do_scan_and_print();
        ESP_LOGI(TAG, "Next scan in 30 s (reset board to scan sooner)");
        vTaskDelay(pdMS_TO_TICKS(30000));
    }
}
