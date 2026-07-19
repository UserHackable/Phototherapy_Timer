/*
 * ESP-IDF app: net_clock
 *
 * LCD1602 progress UI + TM1637 HH:MM + Wi‑Fi (NVS) + DHCP + SNTP.
 *
 * Boot sequence:
 *   1. Init LCD + TM1637; Hello world on LCD
 *   2. Load Wi‑Fi credentials from NVS, connect, show DHCP info
 *   3. Sync time via SNTP
 *   4. LCD = date + 12h AM/PM with seconds; 7-seg = HH:MM (colon blink)
 *
 * Progress messages stay on LCD ~1 s so they are readable.
 * Serial (UART) logs the same steps (password never logged).
 *
 * Wiring:
 *   LCD I²C  SDA=21 SCL=22  VCC=5V
 *   TM1637   CLK=18 DIO=23  VCC=5V
 *
 *   ./scripts/fw idf nvs-wifi
 *   ./scripts/fw idf upload net_clock
 *   ./scripts/fw idf monitor net_clock
 */

#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#include "driver/i2c_master.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_sntp.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "lcd1602_pcf8574.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "tm1637.h"

static const char *TAG = "net_clock";

#define I2C_SDA_GPIO 21
#define I2C_SCL_GPIO 22

#define TM_CLK_GPIO 18
#define TM_DIO_GPIO 23

#define NVS_NS_WIFI "wifi"
#define NVS_KEY_SSID "ssid"
#define NVS_KEY_PASS "password"

#define GOT_IP_BIT BIT0

/* US/Mountain (matches host timedatectl on this project machine). */
#define TZ_POSIX "MST7MDT,M3.2.0,M11.1.0"

#define MSG_HOLD_MS            1000
#define SNTP_WAIT_MAX_RETRIES  30
#define SNTP_PERIOD_US         (6LL * 60 * 60 * 1000000LL) /* 6 hours */
#define WIFI_IP_WAIT_MS        60000

static EventGroupHandle_t s_wifi_events;
static int s_retry;
static volatile bool s_have_ip;
static volatile bool s_need_sntp;
static bool s_sntp_started;
static int64_t s_last_sntp_us;

static esp_netif_ip_info_t s_ip_info;
static char s_ssid[33];

static lcd1602_t s_lcd;
static bool s_lcd_ok;

static tm1637_t s_tm;
static bool s_tm_ok;

/** Show two LCD lines and log them; hold so the user can read. */
static void announce(const char *line0, const char *line1)
{
    const char *l0 = line0 ? line0 : "";
    const char *l1 = line1 ? line1 : "";
    ESP_LOGI(TAG, "%s | %s", l0, l1);
    if (s_lcd_ok) {
        lcd1602_print_line(&s_lcd, 0, l0);
        lcd1602_print_line(&s_lcd, 1, l1);
    }
    vTaskDelay(pdMS_TO_TICKS(MSG_HOLD_MS));
}

/** 12-hour hour for 7-seg (1–12). */
static int hour_12(int hour24)
{
    int h = hour24 % 12;
    return h == 0 ? 12 : h;
}

/** Update TM1637 with HH:MM; colon on even seconds. */
static void led_show_hhmm(const struct tm *t, bool time_valid)
{
    if (!s_tm_ok) {
        return;
    }
    if (!time_valid) {
        /* Dashes-ish: blank pairs with colon so user sees LED is alive */
        tm1637_show_pairs(&s_tm, 0, 0, true);
        return;
    }
    int hh = hour_12(t->tm_hour);
    int mm = t->tm_min;
    bool colon = (t->tm_sec % 2) == 0;
    tm1637_show_pairs(&s_tm, hh, mm, colon);
}

static void event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        s_have_ip = false;
        s_retry++;
        ESP_LOGW(TAG, "Wi‑Fi disconnected, reconnect attempt %d", s_retry);
        esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)data;
        s_ip_info = event->ip_info;
        ESP_LOGI(TAG, "DHCP got ip: " IPSTR " mask " IPSTR " gw " IPSTR,
                 IP2STR(&event->ip_info.ip),
                 IP2STR(&event->ip_info.netmask),
                 IP2STR(&event->ip_info.gw));
        s_retry = 0;
        s_have_ip = true;
        s_need_sntp = true;
        xEventGroupSetBits(s_wifi_events, GOT_IP_BIT);
    }
}

static esp_err_t nvs_load_wifi(char *ssid, size_t ssid_len, char *pass, size_t pass_len)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS_WIFI, NVS_READONLY, &h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open('%s') failed: %s — run ./scripts/fw idf nvs-wifi",
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
    const int step_ms = 1000;
    int waited = 0;
    while (waited < WIFI_IP_WAIT_MS) {
        EventBits_t bits = xEventGroupWaitBits(
            s_wifi_events, GOT_IP_BIT, pdFALSE, pdFALSE, pdMS_TO_TICKS(step_ms));
        if (bits & GOT_IP_BIT) {
            return true;
        }
        waited += step_ms;
        char line1[24];
        snprintf(line1, sizeof(line1), "try %d %ds", s_retry + 1, waited / 1000);
        if (s_lcd_ok) {
            lcd1602_print_line(&s_lcd, 0, "Connecting...");
            lcd1602_print_line(&s_lcd, 1, line1);
        }
        ESP_LOGI(TAG, "waiting for DHCP… %s", line1);
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
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_setservername(1, "time.google.com");
    esp_sntp_init();
    s_sntp_started = true;
}

static bool sync_time_from_network(const char *reason)
{
    if (!s_have_ip) {
        ESP_LOGW(TAG, "SNTP skip (%s): no IP yet", reason);
        return false;
    }

    ESP_LOGI(TAG, "SNTP sync (%s)…", reason);
    sntp_ensure_started();

    if (sntp_get_sync_status() != SNTP_SYNC_STATUS_RESET) {
        esp_sntp_restart();
    }

    int retry = 0;
    while (sntp_get_sync_status() == SNTP_SYNC_STATUS_RESET && ++retry <= SNTP_WAIT_MAX_RETRIES) {
        char line1[24];
        snprintf(line1, sizeof(line1), "wait %d/%d", retry, SNTP_WAIT_MAX_RETRIES);
        if (s_lcd_ok) {
            lcd1602_print_line(&s_lcd, 0, "SNTP sync...");
            lcd1602_print_line(&s_lcd, 1, line1);
        }
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
        return false;
    }

    char buf[64];
    strftime(buf, sizeof(buf), "%Y-%m-%d %I:%M:%S %p %Z", &timeinfo);
    ESP_LOGI(TAG, "network time applied: %s", buf);
    return true;
}

static void display_clock_once(void)
{
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);

    char date_buf[17];
    char time_buf[17];
    bool valid = timeinfo.tm_year >= (2020 - 1900);

    if (valid) {
        strftime(date_buf, sizeof(date_buf), "%Y-%m-%d %a", &timeinfo);
        strftime(time_buf, sizeof(time_buf), "%I:%M:%S %p", &timeinfo);
        ESP_LOGI(TAG, "clock %s %s  LED %02d:%02d%s",
                 date_buf, time_buf,
                 hour_12(timeinfo.tm_hour), timeinfo.tm_min,
                 s_have_ip ? "" : " (offline)");
    } else {
        snprintf(date_buf, sizeof(date_buf), "Time unset");
        snprintf(time_buf, sizeof(time_buf), "wait SNTP");
        ESP_LOGI(TAG, "time unset (internal clock; waiting for SNTP)");
    }

    if (s_lcd_ok) {
        lcd1602_print_line(&s_lcd, 0, date_buf);
        lcd1602_print_line(&s_lcd, 1, time_buf);
    }
    led_show_hhmm(&timeinfo, valid);
}

static bool six_hours_elapsed(void)
{
    if (s_last_sntp_us == 0) {
        return true;
    }
    return (esp_timer_get_time() - s_last_sntp_us) >= SNTP_PERIOD_US;
}

static bool init_lcd(void)
{
    ESP_LOGI(TAG, "init LCD SDA=%d SCL=%d", I2C_SDA_GPIO, I2C_SCL_GPIO);

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
        ESP_LOGE(TAG, "I2C bus create failed");
        return false;
    }

    uint8_t addr = 0;
    if (lcd1602_init(bus, &s_lcd, &addr) != ESP_OK) {
        ESP_LOGE(TAG, "LCD init failed");
        return false;
    }

    lcd1602_backlight(&s_lcd, true);
    lcd1602_clear(&s_lcd);
    s_lcd_ok = true;
    ESP_LOGI(TAG, "LCD ready at 0x%02x", addr);
    return true;
}

static bool init_tm1637(void)
{
    ESP_LOGI(TAG, "init TM1637 CLK=%d DIO=%d", TM_CLK_GPIO, TM_DIO_GPIO);
    if (tm1637_init(&s_tm, TM_CLK_GPIO, TM_DIO_GPIO) != ESP_OK) {
        ESP_LOGE(TAG, "TM1637 init failed");
        return false;
    }
    tm1637_set_brightness(&s_tm, 5);
    /* Bring-up pattern until SNTP fills real time */
    tm1637_show_pairs(&s_tm, 88, 88, true);
    s_tm_ok = true;
    ESP_LOGI(TAG, "TM1637 ready");
    return true;
}

void app_main(void)
{
    ESP_LOGI(TAG, "net_clock starting (LCD + TM1637)");

    /* --- 1. Displays first --- */
    if (!init_lcd()) {
        ESP_LOGW(TAG, "continuing without LCD (serial only)");
    }
    if (!init_tm1637()) {
        ESP_LOGW(TAG, "continuing without TM1637");
    }

    announce("Hello, world!", "ESP32 LCD1602");

    /* --- 2. NVS + credentials --- */
    announce("WiFi setup", "Init NVS...");

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
        announce("WiFi ERROR", "No NVS creds");
        announce("Run host:", "fw idf nvs-wifi");
        ESP_LOGE(TAG, "No usable Wi‑Fi credentials in NVS namespace '%s'", NVS_NS_WIFI);
        while (1) {
            vTaskDelay(pdMS_TO_TICKS(60000));
        }
    }

    strncpy(s_ssid, ssid, sizeof(s_ssid) - 1);

    {
        char ssid_line[17];
        snprintf(ssid_line, sizeof(ssid_line), "%.16s", ssid);
        announce("SSID", ssid_line);
    }

    announce("WiFi STA", "Starting...");
    wifi_start_sta(ssid, pass);

    announce("WiFi STA", "DHCP wait...");
    if (!wait_for_ip()) {
        announce("DHCP failed", "Check WiFi");
        while (1) {
            vTaskDelay(pdMS_TO_TICKS(60000));
        }
    }

    /* --- 3. Show DHCP info --- */
    {
        char ip_line[17];
        char gw_line[17];
        snprintf(ip_line, sizeof(ip_line), IPSTR, IP2STR(&s_ip_info.ip));
        snprintf(gw_line, sizeof(gw_line), IPSTR, IP2STR(&s_ip_info.gw));
        announce("IP address", ip_line);
        announce("Gateway", gw_line);
        snprintf(ip_line, sizeof(ip_line), IPSTR, IP2STR(&s_ip_info.netmask));
        announce("Netmask", ip_line);
    }

    /* --- 4. Network time --- */
    announce("SNTP", "Get time...");
    if (!sync_time_from_network("initial connect")) {
        announce("SNTP failed", "Clock unset");
    } else {
        announce("Time synced", "Network OK");
    }

    ESP_LOGI(TAG, "online — LCD date/AM-PM; LED HH:MM; SNTP every 6 h");

    /* --- 5. Live dual clock --- */
    while (1) {
        display_clock_once();

        if (s_need_sntp || six_hours_elapsed()) {
            const char *why = s_need_sntp ? "reconnect / requested" : "6 hour refresh";
            if (s_have_ip) {
                announce("SNTP refresh", why);
                sync_time_from_network(why);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
