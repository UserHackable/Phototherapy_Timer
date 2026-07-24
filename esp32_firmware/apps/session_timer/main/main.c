/*
 * ESP-IDF app: session_timer
 *
 * Phototherapy-style session UI + wall clock:
 *   - Digits enter MMSS (134 → 1:34, 45 → 0:45); # start; * clear / abort
 *   - TM1637: MM:SS in timer modes; HH:MM wall clock in clock mode
 *   - LCD clock mode: date | bottom-left time + bottom-right requested
 *   - 60 s idle (not while running) → clock mode; any key wakes timer UI
 *   - Lamp: SSR GPIO26 + blue LED GPIO2; fan SSR GPIO27 (30 s rundown after lamp off)
 *   - Piezo end beep GPIO25
 *   - Wi‑Fi from NVS; UDP JSON discovery for server ID + wall clock
 *   - Key A: user list; digit: therapy recommendation → loads MMSS entry
 *   - Lamp off → UDP exposure log (user_id, duration, unix); Guest = id 0
 *   - SNTP (LAN then public) only if discovery does not supply time
 *   - If offline, timer-only and retry Wi‑Fi periodically
 *
 *   ./scripts/fw idf nvs-wifi
 *   ./scripts/fw idf upload session_timer
 *
 * Behavior: docs/features/session_timer.feature
 * Discovery: docs/device-discovery.md
 */

#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#include "cJSON.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/ledc.h"
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
#include "keypad_i2c.h"
#include "lcd1602_pcf8574.h"
#include "lwip/inet.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "tm1637.h"

static const char *TAG = "session_timer";

#define I2C_SDA_GPIO 21
#define I2C_SCL_GPIO 22
#define TM_CLK_GPIO  18
#define TM_DIO_GPIO  23
#define KEYPAD_ADDR  0x20

#define LAMP_LED_GPIO 2
#define SSR_GPIO      26 /* UV lamps */
#define FAN_SSR_GPIO  27 /* cooling fan */
#define PIEZO_GPIO    25

#define BEEP_FREQ_HZ  2000
#define BEEP_MS       200
/** Fan stays on this long after lamps turn off. */
#define FAN_RUNDOWN_MS  30000

#define POLL_MS       15
#define DEBOUNCE_MS   30 /* ~2 polls; keep short so flaky I²C still registers */
#define RELEASE_POLLS 3  /* need a few "up" samples before next key */
#define MAX_DIGITS    4
/** Default programmed session: 30 seconds (MMSS entry 30) until user types. */
#define DEFAULT_ENTRY_MMSS    30
#define DEFAULT_ENTRY_DIGITS  2
#define IDLE_TO_CLOCK_MS  (60 * 1000)
#define WIFI_IP_WAIT_MS   20000
#define WIFI_RETRY_MS     (5 * 60 * 1000)
#define SNTP_WAIT_MAX     20
#define SNTP_PERIOD_US    (6LL * 60 * 60 * 1000000LL)

/* Prefer LAN chrony/NTP (build PC), then public pools. */
#define SNTP_SERVER_LAN        "192.168.1.163"
#define SNTP_SERVER_FALLBACK0  "pool.ntp.org"
#define SNTP_SERVER_FALLBACK1  "time.google.com"

/* UDP JSON discovery — matches server UdpDiscoveryListener (docs/device-discovery.md). */
#define DISCOVERY_PORT        3000
#define DISCOVERY_JSON_V      1
#define DISCOVERY_TIMEOUT_MS  1500
#define DISCOVERY_ATTEMPTS    2
#define DISCOVERY_PERIOD_MS   (5 * 60 * 1000)
#define DISCOVERY_BOOT_WAIT_MS 4000

#define NVS_NS_WIFI  "wifi"
#define NVS_KEY_SSID "ssid"
#define NVS_KEY_PASS "password"
#define GOT_IP_BIT   BIT0
#define TZ_POSIX     "MST7MDT,M3.2.0,M11.1.0"

typedef enum {
    ST_ENTRY = 0,
    ST_RUNNING,
    ST_CLOCK,
    ST_USERS, /* paged user list from server (key A) */
} state_t;

#define LCD_COLS           16
#define USERS_PAGE_MS      1000
#define USERS_MODE_MS      30000
#define USERS_PAGE_MAX     10 /* max pages (one user per page worst case) */

static lcd1602_t s_lcd;
static bool s_lcd_ok;
static tm1637_t s_tm;
static bool s_tm_ok;
static keypad_i2c_t s_kp;

static state_t s_state = ST_ENTRY;
static int s_entry = DEFAULT_ENTRY_MMSS;
static int s_entry_digits = DEFAULT_ENTRY_DIGITS;
static bool s_entry_fresh = true;
static bool s_after_complete = false; /* Done banner until edit/start */
static int s_remain_sec = 0;
/** Planned session length and user captured at lamp-on (for exposure log). */
static int s_session_planned_sec = 0;
static int s_session_user_id = 0;
static int64_t s_last_tick_us = 0;
static int64_t s_last_input_us = 0;

static EventGroupHandle_t s_wifi_events;
static int s_wifi_retry;
static volatile bool s_have_ip;
static volatile bool s_need_sntp;
static volatile bool s_need_discovery;
static bool s_sntp_started;
static bool s_wifi_started;
static int64_t s_last_sntp_us;
static int64_t s_last_wifi_try_us;
static int64_t s_last_discovery_us;

static char s_device_identity[40];
static char s_server_ip[16];
static char s_server_identity[64];
static bool s_have_server;
static bool s_time_from_discovery;

/* User list from server (key A) — household + Guest (id 0) at end. */
#define USER_LIST_MAX 10
typedef struct {
    int id;
    char name[24];
} server_user_t;
static server_user_t s_users[USER_LIST_MAX];
static int s_user_count;
static int s_users_page;
static int s_users_page_count;
/** User index that starts each page (packed into 2×16 LCD cells). */
static int s_users_page_start[USERS_PAGE_MAX];
static int64_t s_users_mode_start_us;
static int64_t s_users_page_start_us;
/** Selected household user after A + digit (0 / empty name = Guest). */
static int s_selected_user_id;
static char s_selected_user_name[24];

/* ---------- lamp / fan / piezo ---------- */

/** Non-zero: esp_timer time when fan should turn off (lamp-off rundown). */
static int64_t s_fan_off_at_us;
static bool s_fan_on;

static void lamp_init(void)
{
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << LAMP_LED_GPIO) | (1ULL << SSR_GPIO) | (1ULL << FAN_SSR_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&io));
    /* Fail-off defaults: lamps, fan, and status LED all low before use. */
    gpio_set_level(LAMP_LED_GPIO, 0);
    gpio_set_level(SSR_GPIO, 0);
    gpio_set_level(FAN_SSR_GPIO, 0);
    s_fan_on = false;
    s_fan_off_at_us = 0;
}

static void fan_set(bool on)
{
    int level = on ? 1 : 0;
    gpio_set_level(FAN_SSR_GPIO, level);
    s_fan_on = on;
    if (on) {
        s_fan_off_at_us = 0; /* cancel pending rundown */
    }
    ESP_LOGI(TAG, "fan %s", on ? "ON" : "OFF");
}

static void lamp_set(bool on)
{
    int level = on ? 1 : 0;
    gpio_set_level(SSR_GPIO, level);
    gpio_set_level(LAMP_LED_GPIO, level); /* status LED mirrors lamps only */
    ESP_LOGI(TAG, "lamp %s", on ? "ON" : "OFF");

    if (on) {
        /* Lamp on → fan on immediately; cancel any rundown. */
        fan_set(true);
    } else {
        /* Lamp off → fan keeps running for FAN_RUNDOWN_MS. */
        if (s_fan_on) {
            s_fan_off_at_us = esp_timer_get_time() + (int64_t)FAN_RUNDOWN_MS * 1000;
            ESP_LOGI(TAG, "fan rundown %d s", FAN_RUNDOWN_MS / 1000);
        }
    }
}

/** Call from main loop: end fan rundown when due. */
static void tick_fan_rundown(void)
{
    if (s_fan_off_at_us <= 0) {
        return;
    }
    if (esp_timer_get_time() >= s_fan_off_at_us) {
        fan_set(false);
        s_fan_off_at_us = 0;
    }
}

static void piezo_init(void)
{
    ledc_timer_config_t timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = BEEP_FREQ_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer));
    ledc_channel_config_t ch = {
        .gpio_num = PIEZO_GPIO,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = LEDC_TIMER_0,
        .duty = 0,
        .hpoint = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ch));
}

static void piezo_beep(void)
{
    ESP_LOGI(TAG, "beep");
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 512);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
    vTaskDelay(pdMS_TO_TICKS(BEEP_MS));
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

/* ---------- time helpers ---------- */

static int entry_to_seconds(int entry)
{
    int mins = entry / 100;
    int secs = entry % 100;
    if (secs > 59) {
        secs = 59;
    }
    if (mins > 99) {
        mins = 99;
    }
    return mins * 60 + secs;
}

/** Convert total seconds to MMSS entry digits (e.g. 30 → 30, 90 → 130). */
static int seconds_to_entry(int total_sec)
{
    if (total_sec < 0) {
        total_sec = 0;
    }
    if (total_sec > 99 * 60 + 59) {
        total_sec = 99 * 60 + 59;
    }
    int mm = total_sec / 60;
    int ss = total_sec % 60;
    return mm * 100 + ss;
}

static int entry_digit_count(int entry)
{
    if (entry <= 0) {
        return 0;
    }
    int d = 0;
    int e = entry;
    while (e > 0 && d < MAX_DIGITS) {
        d++;
        e /= 10;
    }
    return d;
}

static void entry_to_mmss(int entry, int *mm, int *ss)
{
    *mm = entry / 100;
    *ss = entry % 100;
    if (*mm > 99) {
        *mm = 99;
    }
    if (*ss > 99) {
        *ss = 99;
    }
}

static void remain_to_mmss(int total, int *mm, int *ss)
{
    if (total < 0) {
        total = 0;
    }
    *mm = (total / 60) % 100;
    *ss = total % 60;
}

static int hour_12(int hour24)
{
    int h = hour24 % 12;
    return h == 0 ? 12 : h;
}

static bool wall_time_valid(struct tm *out)
{
    time_t now = 0;
    time(&now);
    localtime_r(&now, out);
    return out->tm_year >= (2020 - 1900);
}

static void note_input(void)
{
    s_last_input_us = esp_timer_get_time();
}

static void show_led_mmss(int mm, int ss, bool colon)
{
    if (s_tm_ok) {
        tm1637_show_pairs(&s_tm, mm, ss, colon);
    }
}

/** Wall clock on TM1637 (12h HH:MM, blinking colon). Falls back to entry MM:SS. */
static void show_led_wall_clock(void)
{
    struct tm t;
    if (wall_time_valid(&t)) {
        int hh = hour_12(t.tm_hour);
        bool colon = (t.tm_sec % 2) == 0;
        show_led_mmss(hh, t.tm_min, colon);
    } else {
        int mm, ss;
        entry_to_mmss(s_entry, &mm, &ss);
        show_led_mmss(mm, ss > 59 ? 59 : ss, true);
    }
}

static char s_lcd_cache0[17];
static char s_lcd_cache1[17];

static void lcd_status(const char *line0, const char *line1)
{
    if (!s_lcd_ok) {
        return;
    }
    const char *l0 = line0 ? line0 : "";
    const char *l1 = line1 ? line1 : "";
    /* Skip full redraw if unchanged (clock mode paints often) */
    if (strncmp(s_lcd_cache0, l0, 16) == 0 && strncmp(s_lcd_cache1, l1, 16) == 0) {
        return;
    }
    snprintf(s_lcd_cache0, sizeof(s_lcd_cache0), "%.16s", l0);
    snprintf(s_lcd_cache1, sizeof(s_lcd_cache1), "%.16s", l1);
    lcd1602_print_line(&s_lcd, 0, s_lcd_cache0);
    lcd1602_print_line(&s_lcd, 1, s_lcd_cache1);
}

/** Label for the selected person; "Guest" when none. */
static const char *selected_user_label(void)
{
    if (s_selected_user_id > 0 && s_selected_user_name[0] != '\0') {
        return s_selected_user_name;
    }
    return "Guest";
}

/**
 * LCD top line (16 cols): name left, MM:SS (or M:SS) right-aligned.
 * e.g. "Guest      0:30" / "shirlene   1:00"
 */
static void format_user_time_line(char *line, size_t line_cap, int mm, int ss)
{
    if (line_cap < 2) {
        return;
    }
    if (mm < 0) {
        mm = 0;
    }
    if (mm > 99) {
        mm = 99;
    }
    if (ss < 0) {
        ss = 0;
    }
    if (ss > 59) {
        ss = 59;
    }

    char tfield[8];
    snprintf(tfield, sizeof(tfield), "%d:%02d", mm, ss);

    char right[6];
    size_t tlen = strlen(tfield);
    if (tlen >= 5) {
        memcpy(right, tfield + (tlen - 5), 5);
        right[5] = '\0';
    } else {
        /* right-align into 5 columns */
        size_t pad = 5 - tlen;
        memset(right, ' ', pad);
        memcpy(right + pad, tfield, tlen);
        right[5] = '\0';
    }

    /* 11 name + 5 time = 16 */
    snprintf(line, line_cap, "%-11.11s%5.5s", selected_user_label(), right);
}

/* ---------- Wi‑Fi / SNTP ---------- */

static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        s_have_ip = false;
        s_have_server = false;
        s_wifi_retry++;
        ESP_LOGW(TAG, "Wi‑Fi disconnected, retry %d", s_wifi_retry);
        esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *ev = (ip_event_got_ip_t *)data;
        ESP_LOGI(TAG, "DHCP " IPSTR, IP2STR(&ev->ip_info.ip));
        s_wifi_retry = 0;
        s_have_ip = true;
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
    esp_sntp_init();
    s_sntp_started = true;
}

/** Non-blocking SNTP kick / poll — fallback when discovery has no wall clock. */
static void sntp_poll(const char *reason)
{
    if (!s_have_ip) {
        return;
    }
    /* Prefer server time via UDP discovery; SNTP only if that path failed. */
    if (s_time_from_discovery) {
        return;
    }
    sntp_ensure_started();
    if (s_need_sntp) {
        ESP_LOGI(TAG, "SNTP request (%s) [discovery fallback]", reason ? reason : "");
        if (sntp_get_sync_status() != SNTP_SYNC_STATUS_RESET) {
            esp_sntp_restart();
        }
        s_need_sntp = false;
    }
    if (sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED) {
        s_last_sntp_us = esp_timer_get_time();
    }
}

/** Boot only: SNTP fallback if discovery did not set time. */
static bool sync_time_boot_sntp_fallback(void)
{
    if (!s_have_ip || s_time_from_discovery) {
        struct tm t;
        return wall_time_valid(&t);
    }
    s_need_sntp = true;
    sntp_poll("boot");
    int retry = 0;
    while (sntp_get_sync_status() != SNTP_SYNC_STATUS_COMPLETED && ++retry <= SNTP_WAIT_MAX) {
        vTaskDelay(pdMS_TO_TICKS(250));
    }
    s_last_sntp_us = esp_timer_get_time();
    struct tm t;
    return wall_time_valid(&t);
}

static bool wifi_start_from_nvs(void)
{
    char ssid[33] = {0};
    char pass[65] = {0};
    if (nvs_load_wifi(ssid, sizeof(ssid), pass, sizeof(pass)) != ESP_OK) {
        ESP_LOGW(TAG, "no NVS wifi — timer-only");
        return false;
    }

    s_wifi_events = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));

    wifi_config_t wc = {0};
    strncpy((char *)wc.sta.ssid, ssid, sizeof(wc.sta.ssid) - 1);
    strncpy((char *)wc.sta.password, pass, sizeof(wc.sta.password) - 1);
    wc.sta.threshold.authmode = pass[0] ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wc));
    ESP_ERROR_CHECK(esp_wifi_start());
    s_wifi_started = true;
    s_last_wifi_try_us = esp_timer_get_time();
    ESP_LOGI(TAG, "Wi‑Fi start SSID='%s'", ssid);
    return true;
}

static bool wait_for_ip_brief(void)
{
    if (!s_wifi_events) {
        return false;
    }
    int waited = 0;
    while (waited < WIFI_IP_WAIT_MS) {
        EventBits_t bits = xEventGroupWaitBits(s_wifi_events, GOT_IP_BIT, pdFALSE, pdFALSE,
                                               pdMS_TO_TICKS(500));
        if (bits & GOT_IP_BIT) {
            return true;
        }
        waited += 500;
        if (s_lcd_ok && (waited % 2000) < 500) {
            char l1[17];
            snprintf(l1, sizeof(l1), "WiFi %ds…", waited / 1000);
            lcd_status("Network…", l1);
        }
    }
    return false;
}

/* ---------- UDP JSON discovery (background — does not block keypad) ---------- */

static void build_device_identity(void)
{
    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(s_device_identity, sizeof(s_device_identity),
             "esp32-%02x%02x%02x%02x%02x%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    ESP_LOGI(TAG, "device identity: %s", s_device_identity);
}

/** Apply POSIX TZ string (from discovery or firmware default). */
static void apply_timezone(const char *tz_posix, const char *tz_name)
{
    const char *tz = (tz_posix && tz_posix[0]) ? tz_posix : TZ_POSIX;
    setenv("TZ", tz, 1);
    tzset();
    if (tz_name && tz_name[0]) {
        ESP_LOGI(TAG, "timezone TZ=%s (%s)", tz, tz_name);
    } else {
        ESP_LOGI(TAG, "timezone TZ=%s", tz);
    }
}

static bool apply_unix_time(int64_t unix_sec)
{
    if (unix_sec < 1609459200LL) { /* before 2021-01-01 → ignore */
        return false;
    }
    struct timeval tv = {
        .tv_sec = (time_t)unix_sec,
        .tv_usec = 0,
    };
    if (settimeofday(&tv, NULL) != 0) {
        ESP_LOGW(TAG, "settimeofday failed");
        return false;
    }
    s_time_from_discovery = true;
    s_need_sntp = false;
    s_last_sntp_us = esp_timer_get_time();
    ESP_LOGI(TAG, "wall time from discovery unix=%lld", (long long)unix_sec);
    return true;
}

/** Parse JSON pong; fill identity/ip, timezone, and wall clock from "unix". */
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
    } else {
        strncpy(s_server_identity, "(unknown)", sizeof(s_server_identity) - 1);
    }
    if (cJSON_IsString(ip) && ip->valuestring) {
        strncpy(s_server_ip, ip->valuestring, sizeof(s_server_ip) - 1);
    } else {
        s_server_ip[0] = '\0';
    }

    /* Timezone before settimeofday so localtime uses the server zone. */
    const char *posix = (cJSON_IsString(tz_posix) && tz_posix->valuestring)
                            ? tz_posix->valuestring
                            : NULL;
    const char *tzn = (cJSON_IsString(tz) && tz->valuestring) ? tz->valuestring : NULL;
    apply_timezone(posix, tzn);

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
        ESP_LOGW(TAG, "discovery sendto %s errno %d", astr, errno);
    }
}

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
        ESP_LOGE(TAG, "discovery socket errno %d", errno);
        free(payload);
        return false;
    }

    int yes = 1;
    setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &yes, sizeof(yes));
    struct timeval tv = {
        .tv_sec = DISCOVERY_TIMEOUT_MS / 1000,
        .tv_usec = (DISCOVERY_TIMEOUT_MS % 1000) * 1000,
    };
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    ESP_LOGI(TAG, "discovery payload: %s", payload);

    {
        ip4_addr_t known;
        if (ip4addr_aton(SNTP_SERVER_LAN, &known)) {
            discovery_send(sock, payload, plen, known.addr, "LAN host");
        }
    }

    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    esp_netif_ip_info_t ip_info = {0};
    if (netif && esp_netif_get_ip_info(netif, &ip_info) == ESP_OK) {
        if (ip_info.gw.addr != 0) {
            discovery_send(sock, payload, plen, ip_info.gw.addr, "gateway");
        }
        uint32_t bcast = (ip_info.ip.addr & ip_info.netmask.addr) | ~ip_info.netmask.addr;
        discovery_send(sock, payload, plen, bcast, "subnet");
    }
    discovery_send(sock, payload, plen, htonl(INADDR_BROADCAST), "255.255.255.255");
    free(payload);

    bool found = false;
    int64_t deadline = esp_timer_get_time() + ((int64_t)DISCOVERY_TIMEOUT_MS * 1000);
    while (esp_timer_get_time() < deadline) {
        char rx[512];
        struct sockaddr_in src = {0};
        socklen_t slen = sizeof(src);
        int n = recvfrom(sock, rx, sizeof(rx) - 1, 0, (struct sockaddr *)&src, &slen);
        if (n < 0) {
            break;
        }
        rx[n] = '\0';
        if (!parse_json_pong(rx)) {
            continue;
        }
        s_have_server = true;
        found = true;
        ESP_LOGI(TAG, "discovery pong identity=%s ip=%s time_from_disc=%d",
                 s_server_identity, s_server_ip, (int)s_time_from_discovery);
        break;
    }
    close(sock);
    if (!found) {
        ESP_LOGW(TAG, "discovery: no pong (Rails UDP %d / UFW?)", DISCOVERY_PORT);
    }
    return found;
}

static void run_discovery(const char *reason)
{
    if (!s_have_ip) {
        return;
    }
    ESP_LOGI(TAG, "discovery (%s) identity=%s", reason ? reason : "", s_device_identity);
    bool ok = false;
    for (int i = 0; i < DISCOVERY_ATTEMPTS && !ok; i++) {
        if (i > 0) {
            vTaskDelay(pdMS_TO_TICKS(200));
        }
        ok = discovery_once();
    }
    s_last_discovery_us = esp_timer_get_time();
    s_need_discovery = false;
    if (ok) {
        ESP_LOGI(TAG, "server known: %s @ %s", s_server_identity, s_server_ip);
    }
}

static void discovery_task(void *arg)
{
    (void)arg;
    for (;;) {
        if (s_have_ip) {
            int64_t now = esp_timer_get_time();
            bool due = s_need_discovery || s_last_discovery_us == 0 ||
                       (now - s_last_discovery_us) >= ((int64_t)DISCOVERY_PERIOD_MS * 1000);
            if (due) {
                run_discovery(s_need_discovery ? "ip/reconnect" : "periodic");
            }
        }
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

static void ui_entry_refresh(void);
static void ui_users_page_paint(void);

/** Request users 1–9 from Rails (UDP JSON). Uses known server IP from discovery. */
static bool request_user_list(void)
{
    if (!s_have_ip) {
        ESP_LOGW(TAG, "users: no IP");
        return false;
    }
    if (!s_have_server || s_server_ip[0] == '\0') {
        ESP_LOGW(TAG, "users: no server yet — run discovery first");
        return false;
    }

    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return false;
    }
    cJSON_AddNumberToObject(root, "v", DISCOVERY_JSON_V);
    cJSON_AddStringToObject(root, "type", "users");
    cJSON_AddStringToObject(root, "identity", s_device_identity);
    char *payload = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!payload) {
        return false;
    }

    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        free(payload);
        return false;
    }
    struct timeval tv = {
        .tv_sec = DISCOVERY_TIMEOUT_MS / 1000,
        .tv_usec = (DISCOVERY_TIMEOUT_MS % 1000) * 1000,
    };
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    struct sockaddr_in dest = {0};
    dest.sin_family = AF_INET;
    dest.sin_port = htons(DISCOVERY_PORT);
    if (inet_aton(s_server_ip, &dest.sin_addr) == 0) {
        ESP_LOGW(TAG, "users: bad server ip %s", s_server_ip);
        close(sock);
        free(payload);
        return false;
    }

    ESP_LOGI(TAG, "users request → %s:%d %s", s_server_ip, DISCOVERY_PORT, payload);
    if (sendto(sock, payload, strlen(payload), 0, (struct sockaddr *)&dest, sizeof(dest)) < 0) {
        ESP_LOGW(TAG, "users sendto errno %d", errno);
        close(sock);
        free(payload);
        return false;
    }
    free(payload);

    char rx[1024];
    struct sockaddr_in src = {0};
    socklen_t slen = sizeof(src);
    int n = recvfrom(sock, rx, sizeof(rx) - 1, 0, (struct sockaddr *)&src, &slen);
    close(sock);
    if (n < 0) {
        ESP_LOGW(TAG, "users: no reply");
        return false;
    }
    rx[n] = '\0';
    ESP_LOGI(TAG, "users reply: %s", rx);

    cJSON *j = cJSON_Parse(rx);
    if (!j) {
        return false;
    }
    const cJSON *type = cJSON_GetObjectItemCaseSensitive(j, "type");
    const cJSON *arr = cJSON_GetObjectItemCaseSensitive(j, "users");
    if (!cJSON_IsString(type) || strcasecmp(type->valuestring, "users") != 0 || !cJSON_IsArray(arr)) {
        cJSON_Delete(j);
        return false;
    }

    s_user_count = 0;
    const cJSON *item = NULL;
    cJSON_ArrayForEach(item, arr) {
        if (s_user_count >= USER_LIST_MAX) {
            break;
        }
        const cJSON *id = cJSON_GetObjectItemCaseSensitive(item, "id");
        const cJSON *name = cJSON_GetObjectItemCaseSensitive(item, "name");
        if (!cJSON_IsNumber(id) || !cJSON_IsString(name) || !name->valuestring) {
            continue;
        }
        s_users[s_user_count].id = (int)id->valuedouble;
        strncpy(s_users[s_user_count].name, name->valuestring,
                sizeof(s_users[s_user_count].name) - 1);
        s_users[s_user_count].name[sizeof(s_users[s_user_count].name) - 1] = '\0';
        s_user_count++;
    }
    cJSON_Delete(j);

    ESP_LOGI(TAG, "users loaded: %d", s_user_count);
    for (int i = 0; i < s_user_count; i++) {
        ESP_LOGI(TAG, "  user id=%d name=%s", s_users[i].id, s_users[i].name);
    }
    return s_user_count > 0;
}

/** Find loaded user by server id; returns index or -1. */
static int find_user_index_by_id(int user_id)
{
    for (int i = 0; i < s_user_count; i++) {
        if (s_users[i].id == user_id) {
            return i;
        }
    }
    return -1;
}

/**
 * Request recommended exposure for user_id from Rails (UDP therapy).
 * On success sets *out_sec and optional name buffer; returns true.
 */
static bool request_therapy(int user_id, int *out_sec, char *name_out, size_t name_cap)
{
    if (out_sec) {
        *out_sec = 0;
    }
    if (name_out && name_cap > 0) {
        name_out[0] = '\0';
    }
    if (!s_have_ip || !s_have_server || s_server_ip[0] == '\0') {
        ESP_LOGW(TAG, "therapy: no server");
        return false;
    }
    if (user_id < 0 || user_id > 99) {
        return false;
    }

    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return false;
    }
    cJSON_AddNumberToObject(root, "v", DISCOVERY_JSON_V);
    cJSON_AddStringToObject(root, "type", "therapy");
    cJSON_AddStringToObject(root, "identity", s_device_identity);
    cJSON_AddNumberToObject(root, "user_id", user_id);
    char *payload = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!payload) {
        return false;
    }

    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        free(payload);
        return false;
    }
    struct timeval tv = {
        .tv_sec = DISCOVERY_TIMEOUT_MS / 1000,
        .tv_usec = (DISCOVERY_TIMEOUT_MS % 1000) * 1000,
    };
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    struct sockaddr_in dest = {0};
    dest.sin_family = AF_INET;
    dest.sin_port = htons(DISCOVERY_PORT);
    if (inet_aton(s_server_ip, &dest.sin_addr) == 0) {
        ESP_LOGW(TAG, "therapy: bad server ip %s", s_server_ip);
        close(sock);
        free(payload);
        return false;
    }

    ESP_LOGI(TAG, "therapy request → %s:%d %s", s_server_ip, DISCOVERY_PORT, payload);
    if (sendto(sock, payload, strlen(payload), 0, (struct sockaddr *)&dest, sizeof(dest)) < 0) {
        ESP_LOGW(TAG, "therapy sendto errno %d", errno);
        close(sock);
        free(payload);
        return false;
    }
    free(payload);

    char rx[512];
    struct sockaddr_in src = {0};
    socklen_t slen = sizeof(src);
    int n = recvfrom(sock, rx, sizeof(rx) - 1, 0, (struct sockaddr *)&src, &slen);
    close(sock);
    if (n < 0) {
        ESP_LOGW(TAG, "therapy: no reply");
        return false;
    }
    rx[n] = '\0';
    ESP_LOGI(TAG, "therapy reply: %s", rx);

    cJSON *j = cJSON_Parse(rx);
    if (!j) {
        return false;
    }
    const cJSON *type = cJSON_GetObjectItemCaseSensitive(j, "type");
    const cJSON *err = cJSON_GetObjectItemCaseSensitive(j, "error");
    const cJSON *rec = cJSON_GetObjectItemCaseSensitive(j, "recommended_seconds");
    const cJSON *name = cJSON_GetObjectItemCaseSensitive(j, "name");
    if (!cJSON_IsString(type) || strcasecmp(type->valuestring, "therapy") != 0) {
        cJSON_Delete(j);
        return false;
    }
    if (cJSON_IsString(err) && err->valuestring && err->valuestring[0] != '\0') {
        ESP_LOGW(TAG, "therapy error: %s", err->valuestring);
        cJSON_Delete(j);
        return false;
    }
    if (!cJSON_IsNumber(rec) || rec->valuedouble < 1) {
        cJSON_Delete(j);
        return false;
    }
    int sec = (int)rec->valuedouble;
    if (sec > 99 * 60 + 59) {
        sec = 99 * 60 + 59;
    }
    if (out_sec) {
        *out_sec = sec;
    }
    if (name_out && name_cap > 0 && cJSON_IsString(name) && name->valuestring) {
        strncpy(name_out, name->valuestring, name_cap - 1);
        name_out[name_cap - 1] = '\0';
    }
    cJSON_Delete(j);
    return true;
}

/**
 * Log completed/aborted light-on interval to Rails (UDP exposure).
 * user_id 0 = Guest. duration_seconds = actual lamp-on time.
 * unix = current wall clock at light-off (end time).
 */
static void report_exposure_log(int user_id, int duration_sec)
{
    if (duration_sec < 1) {
        ESP_LOGW(TAG, "exposure log skipped: duration %d", duration_sec);
        return;
    }
    if (!s_have_ip || !s_have_server || s_server_ip[0] == '\0') {
        ESP_LOGW(TAG, "exposure log skipped: no server (user=%d duration=%ds)",
                 user_id, duration_sec);
        return;
    }
    if (user_id < 0) {
        user_id = 0;
    }

    time_t now = 0;
    time(&now);
    if (now < 0) {
        now = 0;
    }

    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return;
    }
    cJSON_AddNumberToObject(root, "v", DISCOVERY_JSON_V);
    cJSON_AddStringToObject(root, "type", "exposure");
    cJSON_AddStringToObject(root, "identity", s_device_identity);
    cJSON_AddNumberToObject(root, "user_id", user_id);
    cJSON_AddNumberToObject(root, "duration_seconds", duration_sec);
    cJSON_AddNumberToObject(root, "unix", (double)now);
    char *payload = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!payload) {
        return;
    }

    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        free(payload);
        return;
    }
    struct timeval tv = {
        .tv_sec = DISCOVERY_TIMEOUT_MS / 1000,
        .tv_usec = (DISCOVERY_TIMEOUT_MS % 1000) * 1000,
    };
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    struct sockaddr_in dest = {0};
    dest.sin_family = AF_INET;
    dest.sin_port = htons(DISCOVERY_PORT);
    if (inet_aton(s_server_ip, &dest.sin_addr) == 0) {
        ESP_LOGW(TAG, "exposure: bad server ip %s", s_server_ip);
        close(sock);
        free(payload);
        return;
    }

    ESP_LOGI(TAG, "exposure log → %s:%d %s", s_server_ip, DISCOVERY_PORT, payload);
    if (sendto(sock, payload, strlen(payload), 0, (struct sockaddr *)&dest, sizeof(dest)) < 0) {
        ESP_LOGW(TAG, "exposure sendto errno %d", errno);
        close(sock);
        free(payload);
        return;
    }
    free(payload);

    char rx[256];
    struct sockaddr_in src = {0};
    socklen_t slen = sizeof(src);
    int n = recvfrom(sock, rx, sizeof(rx) - 1, 0, (struct sockaddr *)&src, &slen);
    close(sock);
    if (n < 0) {
        ESP_LOGW(TAG, "exposure: no ack (log may still be stored)");
        return;
    }
    rx[n] = '\0';
    ESP_LOGI(TAG, "exposure ack: %s", rx);
}

/** Apply recommended seconds into MMSS entry and return to entry mode. */
static void apply_therapy_entry(int user_id, const char *name, int sec)
{
    s_selected_user_id = user_id;
    if (name && name[0]) {
        strncpy(s_selected_user_name, name, sizeof(s_selected_user_name) - 1);
        s_selected_user_name[sizeof(s_selected_user_name) - 1] = '\0';
    } else {
        snprintf(s_selected_user_name, sizeof(s_selected_user_name), "User %d",
                 user_id > 99 ? 99 : (user_id < 0 ? 0 : user_id));
    }
    s_entry = seconds_to_entry(sec);
    s_entry_digits = entry_digit_count(s_entry);
    if (s_entry_digits < 1 && s_entry > 0) {
        s_entry_digits = 1;
    }
    s_entry_fresh = true; /* next digit replaces; # starts with this time */
    s_after_complete = false;
    s_state = ST_ENTRY;
    note_input();
    ESP_LOGI(TAG, "therapy applied user_id=%d name=%s entry=%d (%ds)",
             user_id, s_selected_user_name, s_entry, sec);
    ui_entry_refresh();
}

/** In users mode: digit = user id (0 = Guest) → fetch therapy and load entry. */
static void select_user_by_digit(char digit)
{
    int user_id = digit - '0';
    if (user_id < 0 || user_id > 9) {
        lcd_status("Pick user", "press 0-9");
        note_input();
        return;
    }
    int idx = find_user_index_by_id(user_id);
    if (idx < 0) {
        lcd_status("Unknown id", "try again");
        note_input();
        ESP_LOGW(TAG, "digit %c: user_id %d not in list", digit, user_id);
        return;
    }

    char name_buf[24];
    strncpy(name_buf, s_users[idx].name, sizeof(name_buf) - 1);
    name_buf[sizeof(name_buf) - 1] = '\0';

    lcd_status(name_buf, "therapy…");
    show_led_wall_clock();
    note_input();

    int sec = 0;
    char reply_name[24];
    if (!request_therapy(user_id, &sec, reply_name, sizeof(reply_name))) {
        lcd_status("Therapy fail", name_buf);
        note_input();
        /* Stay in users mode so they can try another digit or wait out. */
        s_state = ST_USERS;
        s_users_page_start_us = esp_timer_get_time();
        vTaskDelay(pdMS_TO_TICKS(800));
        ui_users_page_paint();
        return;
    }
    if (reply_name[0] != '\0') {
        strncpy(name_buf, reply_name, sizeof(name_buf) - 1);
        name_buf[sizeof(name_buf) - 1] = '\0';
    }
    apply_therapy_entry(user_id, name_buf, sec);
}

/**
 * Format one user as "id:name" into a 16-char LCD line (name trimmed if needed).
 * Returns false if uidx is out of range (line cleared).
 */
static bool format_user_line(char *line, size_t line_cap, int uidx)
{
    if (line_cap < 2) {
        return false;
    }
    line[0] = '\0';
    if (uidx < 0 || uidx >= s_user_count) {
        return false;
    }

    int id = s_users[uidx].id;
    if (id < 0) {
        id = 0;
    }
    if (id > 99) {
        id = id % 100;
    }
    const size_t max_vis = LCD_COLS;
    int tlen = snprintf(line, line_cap, "%d:%s", id, s_users[uidx].name);
    if (tlen < 0) {
        line[0] = '\0';
        return false;
    }
    if ((size_t)tlen > max_vis) {
        int id_digits = (id >= 10) ? 2 : 1;
        int name_room = (int)max_vis - id_digits - 1; /* "n:" or "nn:" */
        if (name_room < 1) {
            name_room = 1;
        }
        tlen = snprintf(line, line_cap, "%d:%.*s", id, name_room, s_users[uidx].name);
        if (tlen < 0) {
            line[0] = '\0';
            return false;
        }
        if ((size_t)tlen > max_vis && line_cap > max_vis) {
            line[max_vis] = '\0';
        }
    }
    return true;
}

/** Two users per page (one per LCD line). */
static void recompute_user_pages(void)
{
    s_users_page_count = 0;
    if (s_user_count <= 0) {
        s_users_page_count = 1;
        s_users_page_start[0] = 0;
        return;
    }
    for (int i = 0; i < s_user_count && s_users_page_count < USERS_PAGE_MAX; i += 2) {
        s_users_page_start[s_users_page_count++] = i;
    }
    if (s_users_page_count < 1) {
        s_users_page_count = 1;
        s_users_page_start[0] = 0;
    }
}

/** Paint current page: one user per line, up to two lines. */
static void ui_users_page_paint(void)
{
    if (s_user_count <= 0) {
        lcd_status("Users", "none");
        return;
    }
    if (s_users_page_count < 1) {
        recompute_user_pages();
    }
    if (s_users_page >= s_users_page_count) {
        s_users_page = 0;
    }

    int idx = s_users_page_start[s_users_page];
    char l0[17], l1[17];
    if (!format_user_line(l0, sizeof(l0), idx)) {
        snprintf(l0, sizeof(l0), "Users");
    }
    /* Leave line 1 blank when the last page has only one user. */
    if (!format_user_line(l1, sizeof(l1), idx + 1)) {
        l1[0] = '\0';
    }
    lcd_status(l0, l1);
    /* TM1637 stays on wall clock; page index is LCD-only. */
    show_led_wall_clock();
}

static void enter_clock_mode(void); /* used by tick_users_mode */

static void enter_users_mode(void)
{
    recompute_user_pages();
    s_state = ST_USERS;
    s_users_page = 0;
    s_users_mode_start_us = esp_timer_get_time();
    s_users_page_start_us = s_users_mode_start_us;
    note_input();
    ui_users_page_paint();
    ESP_LOGI(TAG, "users mode: %d users, %d pages (2/page), %d s total",
             s_user_count, s_users_page_count, USERS_MODE_MS / 1000);
}

static void tick_users_mode(void)
{
    int64_t now = esp_timer_get_time();
    if (now - s_users_mode_start_us >= (int64_t)USERS_MODE_MS * 1000) {
        ESP_LOGI(TAG, "users mode timeout → clock");
        enter_clock_mode();
        return;
    }
    if (now - s_users_page_start_us >= (int64_t)USERS_PAGE_MS * 1000) {
        if (s_users_page_count < 1) {
            recompute_user_pages();
        }
        s_users_page = (s_users_page + 1) % s_users_page_count;
        s_users_page_start_us = now;
        ui_users_page_paint();
    }
}

/** Key A: ensure discovery, fetch users, enter paging display. */
static void do_user_list_key(void)
{
    note_input();
    if (!s_have_server || s_server_ip[0] == '\0') {
        lcd_status("Users…", "discover");
        ESP_LOGI(TAG, "key A: no server — retry discovery");
        if (s_have_ip) {
            s_need_discovery = true;
            run_discovery("key A");
        }
    }

    lcd_status("Users…", s_have_server ? s_server_ip : "no server");
    if (request_user_list()) {
        enter_users_mode();
    } else {
        lcd_status("Users fail", s_have_server ? "timeout" : "no server");
        /* Brief fail banner then stay in entry (idle → clock later). */
        s_state = ST_ENTRY;
        note_input();
    }
}

static void network_maintenance(void)
{
    int64_t now = esp_timer_get_time();

    /* SNTP only when discovery has not given us a usable clock. */
    if (s_have_ip && !s_time_from_discovery) {
        if (s_need_sntp) {
            sntp_poll("reconnect");
        } else if (s_last_sntp_us > 0 && (now - s_last_sntp_us) >= SNTP_PERIOD_US) {
            s_need_sntp = true;
            sntp_poll("6h refresh");
        } else {
            sntp_poll(NULL);
        }
    }

    if (!s_wifi_started) {
        return;
    }
    if (!s_have_ip && (now - s_last_wifi_try_us) >= (int64_t)WIFI_RETRY_MS * 1000) {
        s_last_wifi_try_us = now;
        ESP_LOGI(TAG, "Wi‑Fi reconnect attempt");
        esp_wifi_connect();
    }
}

/* ---------- UI modes ---------- */

static void ui_entry_refresh(void)
{
    int mm, ss;
    entry_to_mmss(s_entry, &mm, &ss);
    int sec_show = ss > 59 ? 59 : ss;
    show_led_mmss(mm, sec_show, true);

    char l0[17], l1[17];
    /* Top: selected user (or Guest) left, programmed time right. */
    format_user_time_line(l0, sizeof(l0), mm, sec_show);
    if (s_after_complete && (s_entry_digits > 0 || s_entry > 0)) {
        snprintf(l1, sizeof(l1), "# repeat * clr");
    } else {
        snprintf(l1, sizeof(l1), "# start * clear");
    }
    lcd_status(l0, l1);
}

static void ui_running_refresh(void)
{
    int mm, ss;
    remain_to_mmss(s_remain_sec, &mm, &ss);
    bool colon = (s_remain_sec % 2) == 0;
    show_led_mmss(mm, ss, colon);

    char l0[17], l1[17];
    /* Keep selected user on top; remaining countdown on the right. */
    format_user_time_line(l0, sizeof(l0), mm, ss);
    snprintf(l1, sizeof(l1), "Running * abort");
    lcd_status(l0, l1);
}

static void ui_clock_refresh(void)
{
    struct tm t;
    bool valid = wall_time_valid(&t);
    /* TM1637: wall clock HH:MM */
    show_led_wall_clock();

    char l0[17], l1[17];
    /* Top: selected user (or Guest) + programmed session time (same as entry). */
    int mm, ss;
    entry_to_mmss(s_entry, &mm, &ss);
    int sec_show = ss > 59 ? 59 : ss;
    format_user_time_line(l0, sizeof(l0), mm, sec_show);

    /* Bottom: calendar date, or offline note. */
    if (valid) {
        /* "2026-07-24 Fri" fits 16 cols */
        strftime(l1, sizeof(l1), "%Y-%m-%d %a", &t);
    } else {
        snprintf(l1, sizeof(l1), "No network time");
    }
    lcd_status(l0, l1);
}

static void enter_clock_mode(void)
{
    s_state = ST_CLOCK;
    lamp_set(false);
    ui_clock_refresh();
    ESP_LOGI(TAG, "→ clock mode (idle)");
}

static void enter_entry_mode(void)
{
    s_state = ST_ENTRY;
    note_input();
    ui_entry_refresh();
}

static void clear_entry(void)
{
    /* * restores the 30-second default (not zero); keep selected user. */
    s_entry = DEFAULT_ENTRY_MMSS;
    s_entry_digits = DEFAULT_ENTRY_DIGITS;
    s_entry_fresh = true;
    s_after_complete = false;
}

static void start_session(void)
{
    int total = entry_to_seconds(s_entry);
    if (total <= 0) {
        lcd_status("Need time > 0", "digits then #");
        ESP_LOGW(TAG, "start ignored: zero");
        note_input();
        return;
    }
    s_remain_sec = total;
    s_session_planned_sec = total;
    s_session_user_id = s_selected_user_id > 0 ? s_selected_user_id : 0;
    s_last_tick_us = esp_timer_get_time();
    s_state = ST_RUNNING;
    s_entry_fresh = true;
    s_after_complete = false;
    note_input();
    lamp_set(true);
    ui_running_refresh();
    ESP_LOGI(TAG, "start %d s (entry %d) user_id=%d", total, s_entry, s_session_user_id);
}

/** Actual lamp-on seconds for the session that just ended. */
static int session_elapsed_sec(void)
{
    int elapsed = s_session_planned_sec - s_remain_sec;
    if (elapsed < 0) {
        elapsed = 0;
    }
    if (elapsed > s_session_planned_sec && s_session_planned_sec > 0) {
        elapsed = s_session_planned_sec;
    }
    return elapsed;
}

static void session_complete(void)
{
    int elapsed = session_elapsed_sec();
    int uid = s_session_user_id;
    lamp_set(false);
    piezo_beep();
    report_exposure_log(uid, elapsed);
    s_state = ST_ENTRY;
    s_entry_fresh = true;
    s_after_complete = true;
    note_input(); /* start idle timer for return to clock */
    ui_entry_refresh();
    ESP_LOGI(TAG, "complete — sticky entry %d logged %ds user=%d", s_entry, elapsed, uid);
}

static void abort_to_entry(void)
{
    int elapsed = session_elapsed_sec();
    int uid = s_session_user_id;
    lamp_set(false);
    if (elapsed >= 1) {
        report_exposure_log(uid, elapsed);
    }
    s_state = ST_ENTRY;
    s_entry_fresh = true;
    s_after_complete = false;
    note_input();
    ui_entry_refresh();
    ESP_LOGI(TAG, "aborted — sticky entry %d logged %ds user=%d", s_entry, elapsed, uid);
}

static void on_key(char k)
{
    /* Users paging: digit selects user (therapy); A refreshes; other keys leave. */
    if (s_state == ST_USERS) {
        if (k == 'A') {
            do_user_list_key();
            return;
        }
        if (k >= '0' && k <= '9') {
            select_user_by_digit(k);
            return;
        }
        s_state = ST_ENTRY;
        note_input();
        if (k == 'B' || k == 'C' || k == 'D') {
            ui_entry_refresh();
            return;
        }
        /* Fall through for * / # */
    }

    /* Clock mode: any key wakes into entry handling */
    if (s_state == ST_CLOCK) {
        s_state = ST_ENTRY;
        note_input();
        if (k == 'A') {
            do_user_list_key();
            return;
        }
        if (k == 'B' || k == 'C' || k == 'D') {
            s_entry_fresh = (s_entry_digits > 0 || s_entry > 0);
            ui_entry_refresh();
            return;
        }
        /* Fall through to process * # digit as entry keys */
    }

    if (s_state == ST_RUNNING) {
        if (k == '*') {
            abort_to_entry();
        }
        return;
    }

    /* ST_ENTRY */
    note_input();

    if (k >= '0' && k <= '9') {
        if (s_entry_fresh) {
            s_entry = k - '0';
            s_entry_digits = 1;
            s_entry_fresh = false;
            s_after_complete = false;
        } else if (s_entry_digits >= MAX_DIGITS) {
            s_entry = (s_entry % 1000) * 10 + (k - '0');
        } else {
            s_entry = s_entry * 10 + (k - '0');
            s_entry_digits++;
        }
        ui_entry_refresh();
        return;
    }
    if (k == '*') {
        clear_entry();
        ui_entry_refresh();
        return;
    }
    if (k == '#') {
        start_session();
        return;
    }
    if (k == 'A') {
        do_user_list_key();
        return;
    }
    if (k == 'B' || k == 'C' || k == 'D') {
        ui_entry_refresh();
        return;
    }
}

static void tick_running(void)
{
    int64_t now = esp_timer_get_time();
    if (now - s_last_tick_us < 1000000LL) {
        int mm, ss;
        remain_to_mmss(s_remain_sec, &mm, &ss);
        bool colon = ((now / 500000LL) % 2) == 0;
        show_led_mmss(mm, ss, colon);
        return;
    }
    int64_t elapsed = (now - s_last_tick_us) / 1000000LL;
    s_last_tick_us += elapsed * 1000000LL;
    s_remain_sec -= (int)elapsed;
    if (s_remain_sec <= 0) {
        s_remain_sec = 0;
        session_complete();
        return;
    }
    ui_running_refresh();
}

static void tick_idle_to_clock(void)
{
    if (s_state == ST_RUNNING || s_state == ST_CLOCK || s_state == ST_USERS) {
        return;
    }
    int64_t idle_us = esp_timer_get_time() - s_last_input_us;
    if (idle_us >= (int64_t)IDLE_TO_CLOCK_MS * 1000) {
        enter_clock_mode();
    }
}

/* ---------- init ---------- */

static bool init_peripherals(void)
{
    lamp_init();
    piezo_init();

    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = I2C_SDA_GPIO,
        .scl_io_num = I2C_SCL_GPIO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    i2c_master_bus_handle_t bus = NULL;
    if (i2c_new_master_bus(&bus_cfg, &bus) != ESP_OK) {
        return false;
    }

    uint8_t lcd_addr = 0;
    if (lcd1602_init(bus, &s_lcd, &lcd_addr) == ESP_OK) {
        lcd1602_backlight(&s_lcd, true);
        lcd1602_clear(&s_lcd);
        s_lcd_ok = true;
    }
    if (tm1637_init(&s_tm, TM_CLK_GPIO, TM_DIO_GPIO) == ESP_OK) {
        tm1637_set_brightness(&s_tm, 5);
        s_tm_ok = true;
    }
    if (keypad_i2c_init(bus, &s_kp, KEYPAD_ADDR) != ESP_OK) {
        ESP_LOGE(TAG, "keypad missing");
        return false;
    }
    return true;
}

void app_main(void)
{
    ESP_LOGI(TAG, "session_timer lamp=%d fan=%d LED=%d piezo=%d",
             SSR_GPIO, FAN_SSR_GPIO, LAMP_LED_GPIO, PIEZO_GPIO);

    esp_err_t nvs = nvs_flash_init();
    if (nvs == ESP_ERR_NVS_NO_FREE_PAGES || nvs == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    } else {
        ESP_ERROR_CHECK(nvs);
    }

    if (!init_peripherals()) {
        ESP_LOGE(TAG, "init failed");
        return;
    }

    build_device_identity();

    lcd_status("Session timer", "WiFi…");
    if (wifi_start_from_nvs()) {
        xTaskCreate(discovery_task, "udp_disc", 4096, NULL, 4, NULL);
        if (wait_for_ip_brief()) {
            /* Prefer wall clock from Rails discovery pong; SNTP only if that fails. */
            s_need_discovery = true;
            int waited = 0;
            while (waited < DISCOVERY_BOOT_WAIT_MS && !s_time_from_discovery) {
                vTaskDelay(pdMS_TO_TICKS(200));
                waited += 200;
            }
            if (!s_time_from_discovery) {
                ESP_LOGW(TAG, "no discovery time — SNTP fallback");
                lcd_status("Session timer", "SNTP…");
                sync_time_boot_sntp_fallback();
            }
        } else {
            ESP_LOGW(TAG, "no DHCP — timer-only for now");
        }
    }

    clear_entry();
    note_input();
    /* Prefer clock if we already have wall time */
    struct tm t;
    if (wall_time_valid(&t)) {
        enter_clock_mode();
    } else {
        enter_entry_mode();
    }

    char pending = '\0';
    int pending_ms = 0;
    int release_count = RELEASE_POLLS; /* start armed */
    bool armed = true;
    int64_t last_clock_paint_us = 0;

    while (1) {
        /* Keypad first so Wi‑Fi / LCD paint cannot starve input */
        char key = '\0';
        bool down = keypad_i2c_scan(&s_kp, &key);
        if (down) {
            release_count = 0;
            if (key == pending) {
                pending_ms += POLL_MS;
            } else {
                pending = key;
                pending_ms = POLL_MS;
            }
            if (armed && pending_ms >= DEBOUNCE_MS) {
                armed = false;
                ESP_LOGI(TAG, "key '%c' state=%d", pending, (int)s_state);
                on_key(pending);
            }
        } else {
            /* Tolerate single missed scans under Wi‑Fi noise */
            if (release_count < RELEASE_POLLS) {
                release_count++;
            }
            if (release_count >= RELEASE_POLLS) {
                pending = '\0';
                pending_ms = 0;
                armed = true;
            }
        }

        network_maintenance();
        tick_fan_rundown();

        if (s_state == ST_RUNNING) {
            tick_running();
        } else if (s_state == ST_USERS) {
            tick_users_mode();
        } else if (s_state == ST_CLOCK) {
            int64_t now = esp_timer_get_time();
            /* 1 s LCD/LED refresh — avoid flooding I²C under Wi‑Fi */
            if (now - last_clock_paint_us >= 1000000LL) {
                ui_clock_refresh();
                last_clock_paint_us = now;
            }
        } else {
            tick_idle_to_clock();
        }

        vTaskDelay(pdMS_TO_TICKS(POLL_MS));
    }
}
