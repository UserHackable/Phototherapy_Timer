/*
 * ESP-IDF app: wifi_connect
 *
 * Reads one network from NVS namespace "wifi" (ssid, password).
 * Connects as STA (DHCP), discovers Rails via UDP JSON, sets wall clock
 * from the server pong; SNTP is fallback if discovery has no time.
 *
 * After online:
 *   - UDP JSON discovery (ping identity, pong identity+ip+unix)
 *   - Print local time every 1 minute
 *   - Re-run discovery periodically; SNTP only if discovery time missing
 *
 * Never logs the password. SSR / lamp path unused.
 *
 * Wire protocol (JSON v1 — server/app/services/udp_discovery_listener.rb):
 *   ESP → :3000  {"v":1,"type":"ping","identity":"esp32-<mac>"}
 *   Server →     {"v":1,"type":"pong","identity":"<host>","ip":"<ip>",
 *                 "unix":1710000000,"iso8601":"..."}
 *
 *   ./scripts/nvs-wifi-provision.sh
 *   ./scripts/fw idf upload wifi_connect
 *   ./scripts/fw idf monitor wifi_connect
 */

#include <string.h>
#include <sys/time.h>
#include <time.h>

#include "cJSON.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_sntp.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "lwip/inet.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"
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

/* UDP JSON discovery — must match Rails UdpDiscoveryListener. */
#define DISCOVERY_PORT           3000
#define DISCOVERY_JSON_V         1
#define DISCOVERY_TIMEOUT_MS     2000
#define DISCOVERY_ATTEMPTS       3
#define DISCOVERY_PERIOD_MS      (5 * 60 * 1000)

static EventGroupHandle_t s_wifi_events;
static int s_retry;

/** Set when we need a network time pull (fallback if discovery has no time). */
static volatile bool s_need_sntp;
static volatile bool s_have_ip;
static volatile bool s_need_discovery;
static bool s_sntp_started;
static int64_t s_last_sntp_us;
static int64_t s_last_discovery_us;

static char s_device_identity[40];
static char s_server_ip[16];
static char s_server_identity[64];
static bool s_have_server;
static bool s_time_from_discovery;

static void event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        s_have_ip = false;
        s_have_server = false;
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
        s_need_discovery = true;
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

/** SNTP fallback when UDP discovery did not set wall clock. */
static void sync_time_from_network(const char *reason)
{
    if (!s_have_ip) {
        ESP_LOGW(TAG, "SNTP skip (%s): no IP yet", reason);
        return;
    }
    if (s_time_from_discovery) {
        ESP_LOGI(TAG, "SNTP skip (%s): time already from discovery", reason);
        return;
    }

    ESP_LOGI(TAG, "SNTP sync (%s) [discovery fallback]…", reason);
    sntp_ensure_started();

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

static void build_device_identity(void)
{
    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(s_device_identity, sizeof(s_device_identity),
             "esp32-%02x%02x%02x%02x%02x%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    ESP_LOGI(TAG, "device identity: %s", s_device_identity);
}

static bool apply_unix_time(int64_t unix_sec)
{
    if (unix_sec < 1609459200LL) {
        return false;
    }
    struct timeval tv = { .tv_sec = (time_t)unix_sec, .tv_usec = 0 };
    if (settimeofday(&tv, NULL) != 0) {
        return false;
    }
    s_time_from_discovery = true;
    s_need_sntp = false;
    s_last_sntp_us = esp_timer_get_time();
    ESP_LOGI(TAG, "wall time from discovery unix=%lld", (long long)unix_sec);
    return true;
}

static bool parse_json_pong(const char *msg)
{
    cJSON *root = cJSON_Parse(msg);
    if (!root) {
        return false;
    }
    const cJSON *type = cJSON_GetObjectItemCaseSensitive(root, "type");
    if (!cJSON_IsString(type) || strcasecmp(type->valuestring, "pong") != 0) {
        cJSON_Delete(root);
        return false;
    }
    const cJSON *ident = cJSON_GetObjectItemCaseSensitive(root, "identity");
    const cJSON *ip = cJSON_GetObjectItemCaseSensitive(root, "ip");
    const cJSON *unix_j = cJSON_GetObjectItemCaseSensitive(root, "unix");
    const cJSON *tz = cJSON_GetObjectItemCaseSensitive(root, "tz");
    const cJSON *tz_posix = cJSON_GetObjectItemCaseSensitive(root, "tz_posix");
    if (cJSON_IsString(ident) && ident->valuestring) {
        strncpy(s_server_identity, ident->valuestring, sizeof(s_server_identity) - 1);
    }
    if (cJSON_IsString(ip) && ip->valuestring) {
        strncpy(s_server_ip, ip->valuestring, sizeof(s_server_ip) - 1);
    }
    if (cJSON_IsString(tz_posix) && tz_posix->valuestring && tz_posix->valuestring[0]) {
        setenv("TZ", tz_posix->valuestring, 1);
        tzset();
        ESP_LOGI(TAG, "TZ from discovery: %s%s%s",
                 tz_posix->valuestring,
                 (cJSON_IsString(tz) && tz->valuestring) ? " (" : "",
                 (cJSON_IsString(tz) && tz->valuestring) ? tz->valuestring : "");
        if (cJSON_IsString(tz) && tz->valuestring) {
            /* close paren only when name present — keep log simple */
            ESP_LOGI(TAG, "tz name=%s", tz->valuestring);
        }
    }
    if (cJSON_IsNumber(unix_j)) {
        apply_unix_time((int64_t)unix_j->valuedouble);
    }
    cJSON_Delete(root);
    return true;
}

static char *build_json_ping(void)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return NULL;
    }
    cJSON_AddNumberToObject(root, "v", DISCOVERY_JSON_V);
    cJSON_AddStringToObject(root, "type", "ping");
    cJSON_AddStringToObject(root, "identity", s_device_identity);
    char *out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return out;
}

static void discovery_send(int sock, const char *payload, size_t plen,
                           uint32_t addr_nbo, const char *label)
{
    struct sockaddr_in dest = {0};
    dest.sin_family = AF_INET;
    dest.sin_port = htons(DISCOVERY_PORT);
    dest.sin_addr.s_addr = addr_nbo;

    char astr[16];
    ip4_addr_t a = { .addr = addr_nbo };
    ip4addr_ntoa_r(&a, astr, sizeof(astr));
    ESP_LOGI(TAG, "discovery PING → %s:%d (%s)", astr, DISCOVERY_PORT, label);

    if (sendto(sock, payload, plen, 0, (struct sockaddr *)&dest, sizeof(dest)) < 0) {
        ESP_LOGW(TAG, "discovery sendto %s failed: errno %d", astr, errno);
    }
}

/**
 * Send JSON discovery pings and wait for a JSON pong (identity, ip, unix time).
 */
static bool discovery_once(void)
{
    if (!s_have_ip) {
        return false;
    }

    char *payload = build_json_ping();
    if (!payload) {
        return false;
    }
    size_t plen = strlen(payload);

    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        ESP_LOGE(TAG, "discovery socket failed: errno %d", errno);
        free(payload);
        return false;
    }

    int yes = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &yes, sizeof(yes)) != 0) {
        ESP_LOGW(TAG, "SO_BROADCAST failed: errno %d", errno);
    }

    struct timeval tv = {
        .tv_sec = DISCOVERY_TIMEOUT_MS / 1000,
        .tv_usec = (DISCOVERY_TIMEOUT_MS % 1000) * 1000,
    };
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    ESP_LOGI(TAG, "discovery payload: %s", payload);

    {
        ip4_addr_t known;
        if (ip4addr_aton(SNTP_SERVER_LAN, &known)) {
            discovery_send(sock, payload, plen, known.addr, "known LAN host");
        }
    }

    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    esp_netif_ip_info_t ip_info = {0};
    if (netif && esp_netif_get_ip_info(netif, &ip_info) == ESP_OK) {
        if (ip_info.gw.addr != 0) {
            discovery_send(sock, payload, plen, ip_info.gw.addr, "gateway");
        }
        uint32_t bcast = (ip_info.ip.addr & ip_info.netmask.addr) | ~ip_info.netmask.addr;
        discovery_send(sock, payload, plen, bcast, "subnet broadcast");
    }

    discovery_send(sock, payload, plen, htonl(INADDR_BROADCAST), "255.255.255.255");
    free(payload);

    bool found = false;
    int64_t deadline = esp_timer_get_time() + ((int64_t)DISCOVERY_TIMEOUT_MS * 1000);
    while (esp_timer_get_time() < deadline) {
        char rx[512];
        struct sockaddr_in src = {0};
        socklen_t slen = sizeof(src);
        int n = recvfrom(sock, rx, sizeof(rx) - 1, 0,
                         (struct sockaddr *)&src, &slen);
        if (n < 0) {
            break;
        }
        rx[n] = '\0';

        if (!parse_json_pong(rx)) {
            continue;
        }

        s_have_server = true;
        found = true;

        char from[16];
        ip4_addr_t from_a = { .addr = src.sin_addr.s_addr };
        ip4addr_ntoa_r(&from_a, from, sizeof(from));
        ESP_LOGI(TAG, "discovery pong from %s: identity=%s ip=%s time_from_disc=%d",
                 from, s_server_identity, s_server_ip, (int)s_time_from_discovery);
        break;
    }

    close(sock);

    if (!found) {
        ESP_LOGW(TAG, "discovery: no server pong (is Rails on UDP %d at %s?)",
                 DISCOVERY_PORT, SNTP_SERVER_LAN);
    }
    return found;
}

static void run_discovery(const char *reason)
{
    if (!s_have_ip) {
        ESP_LOGW(TAG, "discovery skip (%s): no IP", reason);
        return;
    }

    ESP_LOGI(TAG, "discovery start (%s), identity=%s", reason, s_device_identity);

    bool ok = false;
    for (int i = 0; i < DISCOVERY_ATTEMPTS && !ok; i++) {
        if (i > 0) {
            vTaskDelay(pdMS_TO_TICKS(300));
        }
        ok = discovery_once();
    }

    s_last_discovery_us = esp_timer_get_time();
    s_need_discovery = false;

    if (ok) {
        ESP_LOGI(TAG, "server known: identity=%s ip=%s",
                 s_server_identity, s_server_ip);
    }
}

static bool discovery_period_elapsed(void)
{
    if (s_last_discovery_us == 0) {
        return true;
    }
    return (esp_timer_get_time() - s_last_discovery_us) >=
           ((int64_t)DISCOVERY_PERIOD_MS * 1000);
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

    build_device_identity();

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

    /* Prefer wall clock from Rails discovery; SNTP only if that fails. */
    run_discovery("initial connect");
    if (!s_time_from_discovery) {
        sync_time_from_network("initial connect");
    }

    ESP_LOGI(TAG, "online — discovery every %d min; SNTP only if discovery has no time",
             DISCOVERY_PERIOD_MS / 60000);

    while (1) {
        print_internal_time();
        if (s_have_server) {
            ESP_LOGI(TAG, "server %s @ %s", s_server_identity, s_server_ip);
        }

        if (s_need_discovery || discovery_period_elapsed()) {
            const char *why = s_need_discovery ? "reconnect / requested" : "periodic";
            run_discovery(why);
        }

        if (!s_time_from_discovery && (s_need_sntp || six_hours_elapsed())) {
            const char *why = s_need_sntp ? "reconnect / requested" : "6 hour refresh";
            sync_time_from_network(why);
        }

        vTaskDelay(pdMS_TO_TICKS(PRINT_PERIOD_MS));
    }
}
