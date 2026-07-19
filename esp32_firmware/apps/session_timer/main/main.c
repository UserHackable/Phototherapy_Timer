/*
 * ESP-IDF app: session_timer
 *
 * Phototherapy-style session UI + wall clock:
 *   - Digits enter MMSS (134 → 1:34, 45 → 0:45); # start; * clear / abort
 *   - TM1637: MM:SS in timer modes; HH:MM wall clock in clock mode
 *   - LCD clock mode: date | bottom-left time + bottom-right requested
 *   - 60 s idle (not while running) → clock mode; any key wakes timer UI
 *   - Lamp: SSR GPIO26 + blue LED GPIO2; end beep piezo GPIO25
 *   - Wi‑Fi/SNTP from NVS; if offline, timer-only and retry periodically
 *
 *   ./scripts/fw idf nvs-wifi
 *   ./scripts/fw idf upload session_timer
 *
 * Behavior: docs/features/session_timer.feature
 */

#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/ledc.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_sntp.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "keypad_i2c.h"
#include "lcd1602_pcf8574.h"
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
#define SSR_GPIO      26
#define PIEZO_GPIO    25

#define BEEP_FREQ_HZ  2000
#define BEEP_MS       200

#define POLL_MS       15
#define DEBOUNCE_MS   30 /* ~2 polls; keep short so flaky I²C still registers */
#define RELEASE_POLLS 3  /* need a few "up" samples before next key */
#define MAX_DIGITS    4
#define IDLE_TO_CLOCK_MS  (60 * 1000)
#define WIFI_IP_WAIT_MS   20000
#define WIFI_RETRY_MS     (5 * 60 * 1000)
#define SNTP_WAIT_MAX     20
#define SNTP_PERIOD_US    (6LL * 60 * 60 * 1000000LL)

#define NVS_NS_WIFI  "wifi"
#define NVS_KEY_SSID "ssid"
#define NVS_KEY_PASS "password"
#define GOT_IP_BIT   BIT0
#define TZ_POSIX     "MST7MDT,M3.2.0,M11.1.0"

typedef enum {
    ST_ENTRY = 0,
    ST_RUNNING,
    ST_CLOCK,
} state_t;

static lcd1602_t s_lcd;
static bool s_lcd_ok;
static tm1637_t s_tm;
static bool s_tm_ok;
static keypad_i2c_t s_kp;

static state_t s_state = ST_ENTRY;
static int s_entry = 0;
static int s_entry_digits = 0;
static bool s_entry_fresh = true;
static bool s_after_complete = false; /* Done banner until edit/start */
static int s_remain_sec = 0;
static int64_t s_last_tick_us = 0;
static int64_t s_last_input_us = 0;

static EventGroupHandle_t s_wifi_events;
static int s_wifi_retry;
static volatile bool s_have_ip;
static volatile bool s_need_sntp;
static bool s_sntp_started;
static bool s_wifi_started;
static int64_t s_last_sntp_us;
static int64_t s_last_wifi_try_us;

/* ---------- lamp / piezo ---------- */

static void lamp_init(void)
{
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << LAMP_LED_GPIO) | (1ULL << SSR_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&io));
    gpio_set_level(LAMP_LED_GPIO, 0);
    gpio_set_level(SSR_GPIO, 0);
}

static void lamp_set(bool on)
{
    int level = on ? 1 : 0;
    gpio_set_level(SSR_GPIO, level);
    gpio_set_level(LAMP_LED_GPIO, level);
    ESP_LOGI(TAG, "lamp %s", on ? "ON" : "OFF");
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

/** Right-align requested duration into 5 chars: " 1:34" / "01:34" / "     " */
static void format_requested_field(char out[6])
{
    if (s_entry_digits == 0 && s_entry == 0) {
        memcpy(out, "     ", 6);
        return;
    }
    int mm_i, ss_i;
    entry_to_mmss(s_entry, &mm_i, &ss_i);
    unsigned mm = (unsigned)(mm_i < 0 ? 0 : (mm_i > 99 ? 99 : mm_i));
    unsigned sec = (unsigned)(ss_i < 0 ? 0 : (ss_i > 59 ? 59 : ss_i));
    if (mm < 10u) {
        out[0] = ' ';
        out[1] = (char)('0' + mm);
        out[2] = ':';
        out[3] = (char)('0' + (sec / 10u));
        out[4] = (char)('0' + (sec % 10u));
        out[5] = '\0';
    } else {
        out[0] = (char)('0' + (mm / 10u));
        out[1] = (char)('0' + (mm % 10u));
        out[2] = ':';
        out[3] = (char)('0' + (sec / 10u));
        out[4] = (char)('0' + (sec % 10u));
        out[5] = '\0';
    }
}

/* ---------- Wi‑Fi / SNTP ---------- */

static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        s_have_ip = false;
        s_wifi_retry++;
        ESP_LOGW(TAG, "Wi‑Fi disconnected, retry %d", s_wifi_retry);
        esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *ev = (ip_event_got_ip_t *)data;
        ESP_LOGI(TAG, "DHCP " IPSTR, IP2STR(&ev->ip_info.ip));
        s_wifi_retry = 0;
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
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_setservername(1, "time.google.com");
    esp_sntp_init();
    s_sntp_started = true;
}

/** Non-blocking SNTP kick / poll — never stalls the keypad loop for seconds. */
static void sntp_poll(const char *reason)
{
    if (!s_have_ip) {
        return;
    }
    sntp_ensure_started();
    if (s_need_sntp) {
        ESP_LOGI(TAG, "SNTP request (%s)", reason ? reason : "");
        if (sntp_get_sync_status() != SNTP_SYNC_STATUS_RESET) {
            esp_sntp_restart();
        }
        s_need_sntp = false; /* one shot; status checked below */
    }
    if (sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED) {
        s_last_sntp_us = esp_timer_get_time();
    }
}

/** Boot only: wait briefly for first sync (UI already on "Network…"). */
static bool sync_time_boot(void)
{
    if (!s_have_ip) {
        return false;
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

static void network_maintenance(void)
{
    int64_t now = esp_timer_get_time();

    if (s_have_ip) {
        if (s_need_sntp) {
            sntp_poll("reconnect");
        } else if (s_last_sntp_us > 0 && (now - s_last_sntp_us) >= SNTP_PERIOD_US) {
            s_need_sntp = true;
            sntp_poll("6h refresh");
        } else {
            sntp_poll(NULL); /* complete in progress */
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

    char l0[24], l1[24];
    if (s_after_complete && (s_entry_digits > 0 || s_entry > 0)) {
        snprintf(l0, sizeof(l0), "Done %d:%02d", mm, sec_show);
        snprintf(l1, sizeof(l1), "# repeat * clr");
    } else if (s_entry_digits == 0 && s_entry == 0) {
        snprintf(l0, sizeof(l0), "Enter MMSS");
        snprintf(l1, sizeof(l1), "# start * clear");
    } else {
        snprintf(l0, sizeof(l0), "%d = %d:%02d", s_entry, mm, sec_show);
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

    char l0[24], l1[24];
    snprintf(l0, sizeof(l0), "Running...");
    snprintf(l1, sizeof(l1), "%02d:%02d left", mm, ss);
    lcd_status(l0, l1);
}

static void ui_clock_refresh(void)
{
    struct tm t;
    bool valid = wall_time_valid(&t);

    if (valid) {
        int hh = hour_12(t.tm_hour);
        bool colon = (t.tm_sec % 2) == 0;
        show_led_mmss(hh, t.tm_min, colon);
    } else {
        /* No wall time: show requested on 7-seg as MM:SS */
        int mm, ss;
        entry_to_mmss(s_entry, &mm, &ss);
        show_led_mmss(mm, ss > 59 ? 59 : ss, true);
    }

    char l0[17], l1[17];
    char req[6];
    format_requested_field(req);

    if (valid) {
        strftime(l0, sizeof(l0), "%Y-%m-%d %a", &t);
        char clk[12];
        strftime(clk, sizeof(clk), "%I:%M:%S %p", &t);
        /* 11 + 5 = 16: "06:58:05 PM" + " 1:34" */
        snprintf(l1, sizeof(l1), "%-11.11s%5.5s", clk, req);
    } else {
        snprintf(l0, sizeof(l0), "No network time");
        snprintf(l1, sizeof(l1), "%-11.11s%5.5s", "timer only", req);
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
    s_entry = 0;
    s_entry_digits = 0;
    s_entry_fresh = false;
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
    s_last_tick_us = esp_timer_get_time();
    s_state = ST_RUNNING;
    s_entry_fresh = true;
    s_after_complete = false;
    note_input();
    lamp_set(true);
    ui_running_refresh();
    ESP_LOGI(TAG, "start %d s (entry %d)", total, s_entry);
}

static void session_complete(void)
{
    lamp_set(false);
    piezo_beep();
    s_state = ST_ENTRY;
    s_entry_fresh = true;
    s_after_complete = true;
    note_input(); /* start idle timer for return to clock */
    ui_entry_refresh();
    ESP_LOGI(TAG, "complete — sticky entry %d", s_entry);
}

static void abort_to_entry(void)
{
    lamp_set(false);
    s_state = ST_ENTRY;
    s_entry_fresh = true;
    s_after_complete = false;
    note_input();
    ui_entry_refresh();
    ESP_LOGI(TAG, "aborted — sticky entry %d", s_entry);
}

static void on_key(char k)
{
    /* Clock mode: any key wakes into entry handling */
    if (s_state == ST_CLOCK) {
        s_state = ST_ENTRY;
        note_input();
        /* A–D: wake only, show entry UI without changing sticky value */
        if (k == 'A' || k == 'B' || k == 'C' || k == 'D') {
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
    if (k == 'A' || k == 'B' || k == 'C' || k == 'D') {
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
    if (s_state == ST_RUNNING || s_state == ST_CLOCK) {
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
    ESP_LOGI(TAG, "session_timer + clock SSR=%d LED=%d piezo=%d", SSR_GPIO, LAMP_LED_GPIO,
             PIEZO_GPIO);

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

    lcd_status("Session timer", "WiFi…");
    if (wifi_start_from_nvs()) {
        if (wait_for_ip_brief()) {
            sync_time_boot();
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

        if (s_state == ST_RUNNING) {
            tick_running();
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
