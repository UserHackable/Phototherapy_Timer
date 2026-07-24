/*
 * ESP-IDF app: wifi_connect
 *
 * Reads one network from NVS namespace "wifi" (ssid, password).
 * Connects as STA (DHCP), syncs wall time via SNTP.
 *
 * After online:
 *   - Print local time every 1 minute from the internal clock
 *   - Refresh time from the network every 6 hours
 *   - Refresh time again when the link drops and DHCP returns
 *
 * Never logs the password. SSR / lamp path unused.
 *
 *   ./scripts/nvs-wifi-provision.sh
 *   ./scripts/fw idf upload wifi_connect
 *   ./scripts/fw idf monitor wifi_connect
 */

#include <string.h>
#include <sys/time.h>
#include <time.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_sntp.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "wifi_connect";

#define NVS_NS_WIFI "wifi"
#define NVS_KEY_SSID "ssid"
#define NVS_KEY_PASS "password"

#define GOT_IP_BIT BIT0

/* US/Mountain (matches host timedatectl on this project machine). */
#define TZ_POSIX "MST7MDT,M3.2.0,M11.1.0"

/*
 * Prefer the LAN chrony/NTP host (Omarchy build PC), then public pools.
 * IP was 192.168.1.163 on this network — reserve it on the router if it
 * moves, or change SNTP_SERVER_LAN below.
 */
#define SNTP_SERVER_LAN   "192.168.1.163"
#define SNTP_SERVER_FALLBACK0 "pool.ntp.org"
#define SNTP_SERVER_FALLBACK1 "time.google.com"

#define PRINT_PERIOD_MS        (60 * 1000)
#define SNTP_PERIOD_US         (6LL * 60 * 60 * 1000000LL) /* 6 hours */
#define SNTP_WAIT_MAX_RETRIES  30

static EventGroupHandle_t s_wifi_events;
static int s_retry;

/** Set when we need a network time pull (boot, 6h timer, or reconnect). */
static volatile bool s_need_sntp;
static volatile bool s_have_ip;
static bool s_sntp_started;
static int64_t s_last_sntp_us;

static void event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        s_have_ip = false;
        s_retry++;
        ESP_LOGW(TAG, "Wi‑Fi disconnected, reconnect attempt %d", s_retry);
        esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)data;
        ESP_LOGI(TAG, "DHCP got ip: " IPSTR " mask " IPSTR " gw " IPSTR,
                 IP2STR(&event->ip_info.ip),
                 IP2STR(&event->ip_info.netmask),
                 IP2STR(&event->ip_info.gw));
        s_retry = 0;
        s_have_ip = true;
        /* Initial connect and every reconnect after a drop: refresh network time. */
        s_need_sntp = true;
        xEventGroupSetBits(s_wifi_events, GOT_IP_BIT);
    }
}

static esp_err_t nvs_load_wifi(char *ssid, size_t ssid_len, char *pass, size_t pass_len)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS_WIFI, NVS_READONLY, &h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open('%s') failed: %s — run ./scripts/nvs-wifi-provision.sh",
                 NVS_NS_WIFI, esp_err_to_name(err));
        return err;
    }

    size_t len = ssid_len;
    err = nvs_get_str(h, NVS_KEY_SSID, ssid, &len);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs get ssid failed: %s", esp_err_to_name(err));
        nvs_close(h);
        return err;
    }

    len = pass_len;
    err = nvs_get_str(h, NVS_KEY_PASS, pass, &len);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        pass[0] = '\0';
        err = ESP_OK;
    } else if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs get password failed: %s", esp_err_to_name(err));
        nvs_close(h);
        return err;
    }

    nvs_close(h);
    return ESP_OK;
}

static void wifi_start_sta(const char *ssid, const char *pass)
{
    s_wifi_events = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL));

    wifi_config_t wifi_config = {0};
    strncpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char *)wifi_config.sta.password, pass, sizeof(wifi_config.sta.password) - 1);
    if (pass[0] == '\0') {
        wifi_config.sta.threshold.authmode = WIFI_AUTH_OPEN;
    } else {
        wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "connecting to SSID='%s'…", ssid);
}

static bool wait_for_ip(void)
{
    EventBits_t bits = xEventGroupWaitBits(
        s_wifi_events, GOT_IP_BIT, pdFALSE, pdFALSE, pdMS_TO_TICKS(60000));
    if (bits & GOT_IP_BIT) {
        return true;
    }
    ESP_LOGE(TAG, "failed to obtain IP via DHCP");
    return false;
}

static void sntp_ensure_started(void)
{
    if (s_sntp_started) {
        return;
    }
    setenv("TZ", TZ_POSIX, 1);
    tzset();
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, SNTP_SERVER_LAN);
    esp_sntp_setservername(1, SNTP_SERVER_FALLBACK0);
    esp_sntp_setservername(2, SNTP_SERVER_FALLBACK1);
    ESP_LOGI(TAG, "SNTP servers: %s, %s, %s",
             SNTP_SERVER_LAN, SNTP_SERVER_FALLBACK0, SNTP_SERVER_FALLBACK1);
    /* Do not auto-poll forever at library default only — we drive refresh ourselves. */
    esp_sntp_init();
    s_sntp_started = true;
}

/** Pull time from the network once (blocking wait up to ~30 s). */
static void sync_time_from_network(const char *reason)
{
    if (!s_have_ip) {
        ESP_LOGW(TAG, "SNTP skip (%s): no IP yet", reason);
        return;
    }

    ESP_LOGI(TAG, "SNTP sync (%s)…", reason);
    sntp_ensure_started();

    /* Force a fresh request on reconnect / 6h refresh. */
    if (sntp_get_sync_status() != SNTP_SYNC_STATUS_RESET) {
        esp_sntp_restart();
    }

    int retry = 0;
    while (sntp_get_sync_status() == SNTP_SYNC_STATUS_RESET && ++retry <= SNTP_WAIT_MAX_RETRIES) {
        ESP_LOGI(TAG, "waiting for SNTP… (%d/%d)", retry, SNTP_WAIT_MAX_RETRIES);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    s_last_sntp_us = esp_timer_get_time();
    s_need_sntp = false;

    time_t now = 0;
    struct tm timeinfo = {0};
    time(&now);
    localtime_r(&now, &timeinfo);
    if (timeinfo.tm_year < (2020 - 1900)) {
        ESP_LOGW(TAG, "SNTP finished but time still looks unset (year=%d)",
                 timeinfo.tm_year + 1900);
        return;
    }

    char buf[64];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S %Z", &timeinfo);
    ESP_LOGI(TAG, "network time applied: %s", buf);
}

/** Print time from the internal (soft) clock — no network I/O. */
static void print_internal_time(void)
{
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);

    char buf[64];
    if (timeinfo.tm_year >= (2020 - 1900)) {
        strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S %Z", &timeinfo);
        ESP_LOGI(TAG, "time %s%s", buf, s_have_ip ? "" : " (offline, internal clock)");
    } else {
        ESP_LOGI(TAG, "time unset (internal clock; waiting for SNTP)");
    }
}

static bool six_hours_elapsed(void)
{
    if (s_last_sntp_us == 0) {
        return true;
    }
    return (esp_timer_get_time() - s_last_sntp_us) >= SNTP_PERIOD_US;
}

void app_main(void)
{
    ESP_LOGI(TAG, "wifi_connect (NVS) starting");

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    } else {
        ESP_ERROR_CHECK(ret);
    }

    char ssid[33] = {0};
    char pass[65] = {0};
    if (nvs_load_wifi(ssid, sizeof(ssid), pass, sizeof(pass)) != ESP_OK) {
        ESP_LOGE(TAG, "No usable Wi‑Fi credentials in NVS namespace '%s'", NVS_NS_WIFI);
        return;
    }

    wifi_start_sta(ssid, pass);
    if (!wait_for_ip()) {
        return;
    }

    /* First network time pull after DHCP. */
    sync_time_from_network("initial connect");

    ESP_LOGI(TAG, "online — print every 1 min (internal clock); SNTP every 6 h and on reconnect");

    while (1) {
        print_internal_time();

        if (s_need_sntp || six_hours_elapsed()) {
            const char *why = s_need_sntp ? "reconnect / requested" : "6 hour refresh";
            sync_time_from_network(why);
        }

        vTaskDelay(pdMS_TO_TICKS(PRINT_PERIOD_MS));
    }
}
