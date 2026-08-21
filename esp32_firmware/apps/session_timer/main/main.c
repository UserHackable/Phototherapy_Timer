/*
 * ESP-IDF app: session_timer
 *
 * Phototherapy-style session UI + wall clock:
 *   - Digits enter MMSS (134 → 1:34, 45 → 0:45); # start; * clear / abort
 *   - TM1637: MM:SS in timer modes; HH:MM wall clock in clock mode
 *   - LCD clock mode: date + A/P (AM/PM) on bottom; user + session time on top
 *   - 60 s idle (not while running) → clock mode; any key wakes timer UI
 *   - Lamp: SSR GPIO26 + blue LED GPIO2; fan SSR GPIO27 (30 s rundown after lamp off)
 *   - Piezo end beep GPIO25
 *   - Wi‑Fi from NVS; UDP JSON discovery for server ID + wall clock
 *   - Ping reports app + running git version; logs match vs published OTA
 *   - UDP status: mode, user, lamp/fan, LCD lines, LED text (on display change + ping)
 *   - Key A: user list; digit: therapy recommendation → loads MMSS entry
 *   - A then user digit then B then therapy digit assigns in one sequence
 *     (A1B4 = user 1, Eczema). Keys typed during UDP waits are kept.
 *   - Key B: therapy list; digit selects type; psoriasis then skin type I–VI
 *   - Therapy reply optional "message" → show on 16x2 LCD, then entry UI
 *   - Therapy reply step_seconds + max_seconds + initial_seconds
 *   - Key *: restore initial_seconds (30 s until a therapy reply)
 *   - Key C/D: recommended ± step_seconds; stay 0 if recommended is 0
 *   - UDP type "ota" (web /devices): check for LAN firmware now (idle only)
 *   - Watcher: UDP key / status / watch remembers the sender and echoes
 *     status there on state changes. type "unwatch" stops those echoes.
 *     Injected keys set test flag; a real keypad press clears it first.
 *     Test mode: UV SSR stays off; UI / fan / countdown / exposure still run.
 *   - Lamp off → UDP exposure log (user, duration, unix, therapy_id, skin_id)
 *   - Server from UDP discovery + DNS (phototherapy.ami.lan); no build-time LAN IP
 *   - Ignore NVS / pong IPs that are off the current DHCP subnet
 *   - LAN OTA via uh_ota when idle (HTTP Host is the DNS name); see docs/ota.md
 *   - SNTP public pools only if discovery does not supply time
 *   - LCD / TM1637 / keypad optional — run discovery headless if absent
 *   - If offline, timer-only and retry Wi‑Fi periodically
 *
 *   ./scripts/fw idf nvs-wifi
 *   ./scripts/fw idf upload session_timer   # full flash once for dual-OTA table
 *   ./scripts/fw idf ota-publish session_timer
 *
 * Behavior: docs/features/session_timer.feature
 * Discovery: docs/device-discovery.md
 * OTA: docs/ota.md
 */

#include <stdio.h>
#include <string.h>
#include <strings.h>
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
#include "uh_ota.h"

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
#define BEEP_MS       1000
/** Fan stays on this long after lamps turn off. */
#define FAN_RUNDOWN_MS  30000
/** Fluorescent tubes take about this long to strike before UV is useful. */
#define LAMP_WARMUP_MS  2000

#define POLL_MS       15
#define DEBOUNCE_MS   30 /* ~2 polls; keep short so flaky I²C still registers */
#define RELEASE_POLLS 3  /* need a few "up" samples before next key */
#define MAX_DIGITS    4
/** Default first-session dose (seconds) until a therapy reply supplies initial_seconds. */
#define DEFAULT_INITIAL_SECONDS  30
/** Boot / no-therapy MMSS entry matching DEFAULT_INITIAL_SECONDS. */
#define DEFAULT_ENTRY_MMSS    30
#define DEFAULT_ENTRY_DIGITS  2
/** Default C/D increment when therapy reply omits step_seconds. */
#define DEFAULT_STEP_SECONDS  10
/** Stock timer maximum (20:00) until a therapy reply supplies max_seconds. */
#define DEFAULT_MAX_SECONDS   (20 * 60)
#define MAX_SESSION_SEC       (99 * 60 + 59)
#define IDLE_TO_CLOCK_MS  (60 * 1000)
#define WIFI_IP_WAIT_MS   20000
#define WIFI_RETRY_MS     (5 * 60 * 1000)
#define SNTP_WAIT_MAX     20
#define SNTP_PERIOD_US    (6LL * 60 * 60 * 1000000LL)

/* Public SNTP only — wall clock prefers UDP discovery pong (no hardcoded LAN IP). */
#define SNTP_SERVER0  "pool.ntp.org"
#define SNTP_SERVER1  "time.google.com"

/* UDP JSON discovery — matches server UdpDiscoveryListener (docs/device-discovery.md). */
#define DISCOVERY_PORT        3000
#define DISCOVERY_JSON_V      1
/** Shop DNS names for the Rails host (kamal-proxy Host header; not a numeric IP). */
#define SERVER_DNS_HOST0      "phototherapy.ami.lan"
#define SERVER_DNS_HOST1      "phototherapy.lan"
#define DISCOVERY_TIMEOUT_MS  2000
#define DISCOVERY_ATTEMPTS    3
#define DISCOVERY_PERIOD_MS   (5 * 60 * 1000)
#define DISCOVERY_BOOT_WAIT_MS 6000
/** Min gap between standalone status datagrams (ping always carries a snapshot). */
#define STATUS_MIN_MS         250
/** How often to poll LAN firmware when idle (not while lamp session). */
#define OTA_CHECK_PERIOD_MS   (15 * 60 * 1000)
/** Delay after boot before first OTA check (let discovery settle, mark valid). */
#define OTA_BOOT_DELAY_MS     20000

#define NVS_NS_WIFI     "wifi"
#define NVS_KEY_SSID    "ssid"
#define NVS_KEY_PASS    "password"
#define NVS_NS_DISC     "discovery"
#define NVS_KEY_SRV_IP  "server_ip"
#define GOT_IP_BIT      BIT0
#define TZ_POSIX        "MST7MDT,M3.2.0,M11.1.0"

typedef enum {
    ST_ENTRY = 0,
    ST_RUNNING,
    ST_CLOCK,
    ST_USERS,     /* paged user list from server (key A) */
    ST_THERAPIES, /* paged therapy types (key B) */
    ST_SKINS,     /* paged skin types after a therapy that needs one */
} state_t;

#define LCD_COLS           16
#define USERS_PAGE_MS      1000
#define USERS_MODE_MS      30000
#define USERS_PAGE_MAX     10 /* max pages (one user per page worst case) */
/** How long to hold a therapy reply message on the LCD before entry UI. */
#define THERAPY_MSG_HOLD_MS 5000
/** Buffer for optional therapy "message" (display uses at most 2×16). */
#define THERAPY_MSG_CAP     64

/** Bottom LCD line kept after therapy message (last exposure detail). */
static char s_last_session_bottom[17];
static bool s_have_last_session_bottom;

static lcd1602_t s_lcd;
static bool s_lcd_ok;
static tm1637_t s_tm;
static bool s_tm_ok;
static keypad_i2c_t s_kp;
static bool s_kp_ok;

/* Keys typed during blocking UDP / LCD holds are queued, then drained. */
#define KEY_Q_CAP 8
static char s_key_q[KEY_Q_CAP];
static uint8_t s_key_q_head;
static uint8_t s_key_q_n;
static char s_key_pending;
static int s_key_pending_ms;
static int s_key_release_count = RELEASE_POLLS;
static bool s_key_armed = true;
/** True only for UDP-injected keys; a real keypad press clears this first. */
static bool s_test_flag;
static void mark_status_dirty(void);
static void maybe_send_status(void);
static void drop_off_subnet_server_addrs(void);

static void key_q_push(char k)
{
    if (s_key_q_n >= KEY_Q_CAP) {
        return;
    }
    s_key_q[(s_key_q_head + s_key_q_n) % KEY_Q_CAP] = k;
    s_key_q_n++;
}

static bool key_q_pop(char *out)
{
    if (s_key_q_n == 0) {
        return false;
    }
    if (out) {
        *out = s_key_q[s_key_q_head];
    }
    s_key_q_head = (s_key_q_head + 1) % KEY_Q_CAP;
    s_key_q_n--;
    return true;
}

static bool key_q_peek(char *out)
{
    if (s_key_q_n == 0) {
        return false;
    }
    if (out) {
        *out = s_key_q[s_key_q_head];
    }
    return true;
}

/** One debounce scan. elapsed_ms is added to the current press timer. */
static void keypad_collect(int elapsed_ms)
{
    if (!s_kp_ok || elapsed_ms <= 0) {
        return;
    }
    char key = '\0';
    bool down = keypad_i2c_scan(&s_kp, &key);
    if (down) {
        s_key_release_count = 0;
        if (key == s_key_pending) {
            s_key_pending_ms += elapsed_ms;
        } else {
            s_key_pending = key;
            s_key_pending_ms = elapsed_ms;
        }
        if (s_key_armed && s_key_pending_ms >= DEBOUNCE_MS) {
            s_key_armed = false;
            s_test_flag = false;
            key_q_push(s_key_pending);
            mark_status_dirty();
            ESP_LOGI(TAG, "key '%c' queued n=%d test=0 (keypad)", s_key_pending, (int)s_key_q_n);
        }
        return;
    }
    if (s_key_release_count < RELEASE_POLLS) {
        s_key_release_count++;
    }
    if (s_key_release_count >= RELEASE_POLLS) {
        s_key_pending = '\0';
        s_key_pending_ms = 0;
        s_key_armed = true;
    }
}

/** Wait up to timeout_ms, aborting early if another key is already queued. */
static void hold_collecting_keys(int timeout_ms)
{
    int left = timeout_ms;
    while (left > 0 && s_key_q_n == 0) {
        int slice = left > POLL_MS ? POLL_MS : left;
        keypad_collect(slice);
        maybe_send_status();
        vTaskDelay(pdMS_TO_TICKS(slice));
        left -= slice;
    }
}

static state_t s_state = ST_ENTRY;
static int s_entry = DEFAULT_ENTRY_MMSS;
static int s_entry_digits = DEFAULT_ENTRY_DIGITS;
static bool s_entry_fresh = true;
static bool s_after_complete = false; /* Done banner until edit/start */
static int s_remain_sec = 0;
/** Planned session length and user/therapy captured at lamp-on (for exposure log). */
static int s_session_planned_sec = 0;
static int s_session_user_id = 0;
static int s_session_therapy_id = 0;
static int s_session_skin_id = 0;
/** Nonzero while lamps are striking; countdown has not started yet. */
static int64_t s_warmup_until_us;
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
static int64_t s_last_ota_check_us;
static bool s_ota_boot_marked;
/** Set by UDP type "ota" (web poke); maybe_ota_check runs on the next idle pass. */
static volatile bool s_ota_requested;
static int s_cmd_sock = -1;
/** Watcher (LAN host) that receives status on state changes. */
static struct sockaddr_in s_cmd_peer;
static bool s_have_cmd_peer;

static char s_device_identity[40];
static char s_server_ip[16];
/** Last known server for unicast discovery (RAM + NVS); not required for s_have_server. */
static char s_server_ip_hint[16];
/** HTTP Host for OTA — DNS name when it resolves, else a subnet-local IP. */
static char s_server_http_host[40];
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

#define PICK_LIST_MAX 10
typedef struct {
    int id;
    char name[24];
    bool uses_skin_type;
} pick_item_t;
static pick_item_t s_therapies[PICK_LIST_MAX];
static int s_therapy_count;
static pick_item_t s_skins[PICK_LIST_MAX];
static int s_skin_count;
static int s_pending_therapy_id;
static char s_pending_therapy_name[24];
/** Keypad therapy/skin from last therapy reply (logged on lamp-off). */
static int s_therapy_id;
static int s_skin_id;
/** From last therapy reply: C/D step recommended; * restores initial. */
static int s_step_seconds = DEFAULT_STEP_SECONDS;
static int s_max_seconds = DEFAULT_MAX_SECONDS;
static int s_initial_seconds = DEFAULT_INITIAL_SECONDS;
static int s_recommended_seconds;
static bool s_have_recommended;
static int s_last_duration_sec;
static bool s_have_last_duration;

static bool s_status_dirty;
static int64_t s_last_status_us;
static char s_led_cache[8];
static bool s_led_clock;
static bool s_lamp_on;

static void mark_status_dirty(void);
static cJSON *status_object(void);

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
    s_lamp_on = false;
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
    mark_status_dirty();
    ESP_LOGI(TAG, "fan %s", on ? "ON" : "OFF");
}

static void lamp_set(bool on)
{
    /* Test mode: UI / fan / exposure behave as if the lamp is on; UV SSR stays off. */
    int ssr = (on && !s_test_flag) ? 1 : 0;
    int led = on ? 1 : 0; /* LED mirrors logical lamp (safe, not UV) */
    gpio_set_level(SSR_GPIO, ssr);
    gpio_set_level(LAMP_LED_GPIO, led);
    s_lamp_on = on;
    mark_status_dirty();
    ESP_LOGI(TAG, "lamp %s%s", on ? "ON" : "OFF",
             (on && s_test_flag) ? " (test: SSR off)" : "");

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
static int clamp_session_sec(int sec)
{
    int cap = s_max_seconds;
    if (cap < 1 || cap > MAX_SESSION_SEC) {
        cap = MAX_SESSION_SEC;
    }
    if (sec > cap) {
        return cap;
    }
    if (sec < 0) {
        return 0;
    }
    return sec;
}

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

static void show_led_mmss_ex(int mm, int ss, bool colon, bool clock)
{
    if (mm < 0) {
        mm = 0;
    }
    if (mm > 99) {
        mm = 99;
    }
    if (ss < 0) {
        ss = 0;
    }
    if (ss > 99) {
        ss = 99;
    }
    char buf[8];
    snprintf(buf, sizeof(buf), "%02d:%02d", mm, ss);
    if (s_led_clock != clock || strncmp(s_led_cache, buf, sizeof(s_led_cache)) != 0) {
        s_led_clock = clock;
        snprintf(s_led_cache, sizeof(s_led_cache), "%s", buf);
        mark_status_dirty();
    }
    if (s_tm_ok) {
        tm1637_show_pairs(&s_tm, mm, ss, colon);
    }
}

static void show_led_mmss(int mm, int ss, bool colon)
{
    show_led_mmss_ex(mm, ss, colon, false);
}

/** Wall clock on TM1637 (12h HH:MM, blinking colon). Falls back to entry MM:SS. */
static void show_led_wall_clock(void)
{
    struct tm t;
    if (wall_time_valid(&t)) {
        int hh = hour_12(t.tm_hour);
        bool colon = (t.tm_sec % 2) == 0;
        show_led_mmss_ex(hh, t.tm_min, colon, true);
    } else {
        int mm, ss;
        entry_to_mmss(s_entry, &mm, &ss);
        show_led_mmss_ex(mm, ss > 59 ? 59 : ss, true, true);
    }
}

static char s_lcd_cache0[17];
static char s_lcd_cache1[17];

static void lcd_status(const char *line0, const char *line1)
{
    const char *l0 = line0 ? line0 : "";
    const char *l1 = line1 ? line1 : "";
    /* Skip full redraw if unchanged (clock mode paints often) */
    if (strncmp(s_lcd_cache0, l0, 16) == 0 && strncmp(s_lcd_cache1, l1, 16) == 0) {
        return;
    }
    snprintf(s_lcd_cache0, sizeof(s_lcd_cache0), "%.16s", l0);
    snprintf(s_lcd_cache1, sizeof(s_lcd_cache1), "%.16s", l1);
    if (s_lcd_ok) {
        lcd1602_print_line(&s_lcd, 0, s_lcd_cache0);
        lcd1602_print_line(&s_lcd, 1, s_lcd_cache1);
    }
    mark_status_dirty();
}

/**
 * Fit a free-text server message onto the 16x2 LCD (left-aligned, up to 32 chars).
 * Explicit newline forces line break (therapy last-session layout).
 * Otherwise prefer breaking the first line at a space in columns 8-16.
 */
static void lcd_show_message(const char *msg)
{
    char l0[17];
    char l1[17];

    if (!msg) {
        return;
    }
    while (*msg == ' ' || *msg == '\t' || *msg == '\n' || *msg == '\r') {
        msg++;
    }
    if (msg[0] == '\0') {
        return;
    }

    /* Prefer explicit two-line layout from the server. */
    const char *nl = strchr(msg, '\n');
    if (nl) {
        size_t n0 = (size_t)(nl - msg);
        if (n0 > (size_t)LCD_COLS) {
            n0 = (size_t)LCD_COLS;
        }
        snprintf(l0, sizeof(l0), "%.*s", (int)n0, msg);
        for (int i = (int)strlen(l0) - 1; i >= 0 && l0[i] == ' '; i--) {
            l0[i] = '\0';
        }
        const char *rest = nl + 1;
        while (*rest == ' ' || *rest == '\t' || *rest == '\r') {
            rest++;
        }
        snprintf(l1, sizeof(l1), "%.16s", rest);
        lcd_status(l0, l1);
        return;
    }

    size_t len = strlen(msg);
    if (len <= (size_t)LCD_COLS) {
        snprintf(l0, sizeof(l0), "%s", msg);
        l1[0] = '\0';
        lcd_status(l0, l1);
        return;
    }

    int break_at = LCD_COLS;
    for (int i = LCD_COLS; i >= 8; i--) {
        if (msg[i] == ' ' || msg[i] == '\t') {
            break_at = i;
            break;
        }
    }
    if (break_at < 1) {
        break_at = LCD_COLS;
    }

    snprintf(l0, sizeof(l0), "%.*s", break_at, msg);
    for (int i = (int)strlen(l0) - 1; i >= 0 && l0[i] == ' '; i--) {
        l0[i] = '\0';
    }

    const char *rest = msg + break_at;
    while (*rest == ' ' || *rest == '\t') {
        rest++;
    }
    snprintf(l1, sizeof(l1), "%.16s", rest);
    lcd_status(l0, l1);
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
        if (s_cmd_sock >= 0) {
            close(s_cmd_sock);
            s_cmd_sock = -1;
        }
        ESP_LOGW(TAG, "Wi‑Fi disconnected, retry %d", s_wifi_retry);
        esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *ev = (ip_event_got_ip_t *)data;
        ESP_LOGI(TAG, "DHCP " IPSTR, IP2STR(&ev->ip_info.ip));
        s_wifi_retry = 0;
        s_have_ip = true;
        drop_off_subnet_server_addrs();
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
    esp_sntp_setservername(0, SNTP_SERVER0);
    esp_sntp_setservername(1, SNTP_SERVER1);
    ESP_LOGI(TAG, "SNTP servers (discovery fallback): %s, %s", SNTP_SERVER0, SNTP_SERVER1);
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

static bool sta_ip_info(esp_netif_ip_info_t *out)
{
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (!netif || !out) {
        return false;
    }
    return esp_netif_get_ip_info(netif, out) == ESP_OK && out->ip.addr != 0;
}

static bool ipv4_on_sta_subnet(uint32_t addr_nbo)
{
    esp_netif_ip_info_t info;
    if (!sta_ip_info(&info) || addr_nbo == 0) {
        return false;
    }
    return (addr_nbo & info.netmask.addr) == (info.ip.addr & info.netmask.addr);
}

static bool ipv4_str_on_sta_subnet(const char *ip)
{
    ip4_addr_t a;
    if (!ip || !ip[0] || ip4addr_aton(ip, &a) == 0) {
        return false;
    }
    return ipv4_on_sta_subnet(a.addr);
}

/** Load last discovered server IP for unicast hint (no hard-coded LAN host). */
static void nvs_load_server_ip_hint(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS_DISC, NVS_READONLY, &h) != ESP_OK) {
        return;
    }
    size_t len = sizeof(s_server_ip_hint);
    if (nvs_get_str(h, NVS_KEY_SRV_IP, s_server_ip_hint, &len) == ESP_OK &&
        s_server_ip_hint[0] != '\0') {
        ESP_LOGI(TAG, "discovery hint from NVS: %s", s_server_ip_hint);
    } else {
        s_server_ip_hint[0] = '\0';
    }
    nvs_close(h);
}

static void nvs_clear_server_ip_hint(void)
{
    nvs_handle_t h;
    s_server_ip_hint[0] = '\0';
    if (nvs_open(NVS_NS_DISC, NVS_READWRITE, &h) != ESP_OK) {
        return;
    }
    nvs_erase_key(h, NVS_KEY_SRV_IP);
    nvs_commit(h);
    nvs_close(h);
}

static void nvs_save_server_ip_hint(const char *ip)
{
    if (!ip || !ip[0] || !ipv4_str_on_sta_subnet(ip)) {
        return;
    }
    nvs_handle_t h;
    if (nvs_open(NVS_NS_DISC, NVS_READWRITE, &h) != ESP_OK) {
        return;
    }
    if (nvs_set_str(h, NVS_KEY_SRV_IP, ip) == ESP_OK) {
        nvs_commit(h);
    }
    nvs_close(h);
    strncpy(s_server_ip_hint, ip, sizeof(s_server_ip_hint) - 1);
    s_server_ip_hint[sizeof(s_server_ip_hint) - 1] = '\0';
}

static void drop_off_subnet_server_addrs(void)
{
    if (s_server_ip[0] && !ipv4_str_on_sta_subnet(s_server_ip)) {
        ESP_LOGW(TAG, "drop off-subnet server ip %s", s_server_ip);
        s_server_ip[0] = '\0';
        s_have_server = false;
    }
    if (s_server_ip_hint[0] && !ipv4_str_on_sta_subnet(s_server_ip_hint)) {
        ESP_LOGW(TAG, "drop off-subnet NVS hint %s", s_server_ip_hint);
        nvs_clear_server_ip_hint();
    }
}

static void remember_http_host(const char *host)
{
    if (!host || !host[0]) {
        return;
    }
    if (s_server_http_host[0] && strcmp(s_server_http_host, SERVER_DNS_HOST0) == 0) {
        return;
    }
    strncpy(s_server_http_host, host, sizeof(s_server_http_host) - 1);
    s_server_http_host[sizeof(s_server_http_host) - 1] = '\0';
}

static bool resolve_hostname4(const char *name, uint32_t *addr_nbo)
{
    if (!name || !name[0] || !addr_nbo) {
        return false;
    }
    struct addrinfo hints = {
        .ai_family = AF_INET,
        .ai_socktype = SOCK_DGRAM,
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

/**
 * Parse JSON pong; fill identity/ip, timezone, and wall clock from "unix".
 * @param from_ip  UDP source (used if pong omits "ip")
 */
static bool parse_json_pong(const char *msg, const char *from_ip)
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
        s_server_identity[sizeof(s_server_identity) - 1] = '\0';
    } else {
        strncpy(s_server_identity, "(unknown)", sizeof(s_server_identity) - 1);
    }

    s_server_ip[0] = '\0';
    const char *advertised = (cJSON_IsString(ip) && ip->valuestring && ip->valuestring[0])
                                 ? ip->valuestring
                                 : NULL;
    if (ipv4_str_on_sta_subnet(advertised)) {
        strncpy(s_server_ip, advertised, sizeof(s_server_ip) - 1);
    } else {
        if (advertised) {
            ESP_LOGW(TAG, "pong ip %s is off-subnet; using UDP source %s",
                     advertised, from_ip && from_ip[0] ? from_ip : "-");
        }
        if (from_ip && from_ip[0]) {
            strncpy(s_server_ip, from_ip, sizeof(s_server_ip) - 1);
        }
    }
    s_server_ip[sizeof(s_server_ip) - 1] = '\0';

    /* Timezone before settimeofday so localtime uses the server zone. */
    const char *posix = (cJSON_IsString(tz_posix) && tz_posix->valuestring)
                            ? tz_posix->valuestring
                            : NULL;
    const char *tzn = (cJSON_IsString(tz) && tz->valuestring) ? tz->valuestring : NULL;
    apply_timezone(posix, tzn);

    if (cJSON_IsNumber(unix_j)) {
        apply_unix_time((int64_t)unix_j->valuedouble);
    }

    const cJSON *pub = cJSON_GetObjectItemCaseSensitive(root, "published_version");
    const char *running = uh_ota_running_version();
    if (cJSON_IsString(pub) && pub->valuestring && pub->valuestring[0]) {
        if (strcmp(pub->valuestring, running) == 0) {
            ESP_LOGI(TAG, "firmware matches published %s", running);
        } else {
            ESP_LOGW(TAG, "firmware mismatch: running=%s published=%s",
                     running, pub->valuestring);
        }
    } else {
        ESP_LOGI(TAG, "firmware running=%s (no published_version in pong)", running);
    }

    cJSON_Delete(root);
    return true;
}

static void mark_status_dirty(void)
{
    s_status_dirty = true;
}

static const char *ui_state_name(void)
{
    switch (s_state) {
    case ST_RUNNING:
        return (s_warmup_until_us > 0) ? "warming" : "running";
    case ST_CLOCK:
        return "clock";
    case ST_USERS:
        return "users";
    case ST_THERAPIES:
        return "therapies";
    case ST_SKINS:
        return "skins";
    default:
        return "entry";
    }
}

static cJSON *status_object(void)
{
    cJSON *st = cJSON_CreateObject();
    if (!st) {
        return NULL;
    }
    cJSON_AddStringToObject(st, "state", ui_state_name());
    cJSON_AddNumberToObject(st, "user_id", s_selected_user_id > 0 ? s_selected_user_id : 0);
    cJSON_AddStringToObject(st, "user", selected_user_label());

    int mm, ss;
    entry_to_mmss(s_entry, &mm, &ss);
    if (mm < 0) {
        mm = 0;
    }
    if (mm > 99) {
        mm = 99;
    }
    int sec_show = ss > 59 ? 59 : ss;
    if (sec_show < 0) {
        sec_show = 0;
    }
    char entry[8];
    snprintf(entry, sizeof(entry), "%d:%02d", mm, sec_show);
    cJSON_AddStringToObject(st, "entry", entry);
    cJSON_AddNumberToObject(st, "remain_seconds", s_state == ST_RUNNING ? s_remain_sec : 0);
    cJSON_AddNumberToObject(st, "planned_seconds", s_session_planned_sec);
    cJSON_AddBoolToObject(st, "lamp", s_lamp_on);
    cJSON_AddBoolToObject(st, "fan", s_fan_on);
    cJSON_AddBoolToObject(st, "after_complete", s_after_complete);
    cJSON_AddBoolToObject(st, "test", s_test_flag);
    cJSON_AddBoolToObject(st, "warmup", s_state == ST_RUNNING && s_warmup_until_us > 0);

    cJSON *lcd = cJSON_CreateArray();
    if (lcd) {
        cJSON_AddItemToArray(lcd, cJSON_CreateString(s_lcd_cache0));
        cJSON_AddItemToArray(lcd, cJSON_CreateString(s_lcd_cache1));
        cJSON_AddItemToObject(st, "lcd", lcd);
    }
    cJSON_AddStringToObject(st, "led", s_led_cache[0] ? s_led_cache : "");
    cJSON_AddStringToObject(st, "led_kind", s_led_clock ? "clock" : "timer");
    return st;
}

static void remember_cmd_peer(const struct sockaddr_in *src)
{
    if (!src || src->sin_addr.s_addr == 0) {
        return;
    }
    s_cmd_peer = *src;
    s_have_cmd_peer = true;
    ESP_LOGI(TAG, "watcher %s:%u", inet_ntoa(src->sin_addr),
             (unsigned)ntohs(src->sin_port));
}

static bool watcher_is(const struct sockaddr_in *src)
{
    return s_have_cmd_peer && src &&
           src->sin_addr.s_addr == s_cmd_peer.sin_addr.s_addr;
}

static bool forget_cmd_peer(const struct sockaddr_in *src)
{
    if (!s_have_cmd_peer) {
        return true;
    }
    if (src && !watcher_is(src)) {
        ESP_LOGW(TAG, "unwatch ignored (not current watcher)");
        return false;
    }
    ESP_LOGI(TAG, "watcher cleared");
    memset(&s_cmd_peer, 0, sizeof(s_cmd_peer));
    s_have_cmd_peer = false;
    return true;
}

static void send_status_to(const struct sockaddr_in *dest, const char *payload)
{
    if (!dest || !payload) {
        return;
    }
    int sock = s_cmd_sock;
    bool owned = false;
    if (sock < 0) {
        sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (sock < 0) {
            return;
        }
        owned = true;
    }
    char ip[16];
    inet_ntop(AF_INET, &dest->sin_addr, ip, sizeof(ip));
    ESP_LOGI(TAG, "status → %s:%u %s", ip, (unsigned)ntohs(dest->sin_port), payload);
    if (sendto(sock, payload, strlen(payload), 0, (const struct sockaddr *)dest,
               sizeof(*dest)) < 0) {
        ESP_LOGW(TAG, "status sendto errno %d", errno);
    }
    if (owned) {
        close(sock);
    }
}

static char *build_status_payload(void)
{
    if (s_device_identity[0] == '\0') {
        return NULL;
    }
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return NULL;
    }
    cJSON_AddNumberToObject(root, "v", DISCOVERY_JSON_V);
    cJSON_AddStringToObject(root, "type", "status");
    cJSON_AddStringToObject(root, "identity", s_device_identity);
    cJSON_AddStringToObject(root, "app", "session_timer");
    cJSON_AddStringToObject(root, "version", uh_ota_running_version());
    cJSON *st = status_object();
    if (st) {
        cJSON_AddItemToObject(root, "status", st);
    }
    char *payload = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return payload;
}

static void send_status_datagram(void)
{
    if (!s_have_ip) {
        return;
    }
    char *payload = build_status_payload();
    if (!payload) {
        return;
    }

    if (s_have_server && s_server_ip[0] != '\0') {
        struct sockaddr_in dest = {0};
        dest.sin_family = AF_INET;
        dest.sin_port = htons(DISCOVERY_PORT);
        if (inet_aton(s_server_ip, &dest.sin_addr) != 0) {
            send_status_to(&dest, payload);
        }
    }
    if (s_have_cmd_peer) {
        send_status_to(&s_cmd_peer, payload);
    }
    free(payload);
}

static void maybe_send_status(void)
{
    if (!s_status_dirty) {
        return;
    }
    int64_t now = esp_timer_get_time();
    if (s_last_status_us > 0 && (now - s_last_status_us) < (int64_t)STATUS_MIN_MS * 1000) {
        return;
    }
    if (!s_have_ip) {
        return;
    }
    if ((!s_have_server || s_server_ip[0] == '\0') && !s_have_cmd_peer) {
        return;
    }
    s_status_dirty = false;
    s_last_status_us = now;
    send_status_datagram();
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
    cJSON_AddStringToObject(root, "app", "session_timer");
    cJSON_AddStringToObject(root, "version", uh_ota_running_version());
    cJSON *st = status_object();
    if (st) {
        cJSON_AddItemToObject(root, "status", st);
    }
    /* Report our STA IP so the server can store it even if NAT rewrites peer. */
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    esp_netif_ip_info_t ip_info = {0};
    if (netif && esp_netif_get_ip_info(netif, &ip_info) == ESP_OK && ip_info.ip.addr != 0) {
        char self_ip[16];
        ip4_addr_t a = { .addr = ip_info.ip.addr };
        ip4addr_ntoa_r(&a, self_ip, sizeof(self_ip));
        cJSON_AddStringToObject(root, "ip", self_ip);
    }
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
        .tv_sec = 0,
        .tv_usec = POLL_MS * 1000,
    };
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    ESP_LOGI(TAG, "discovery payload: %s", payload);

    /* DNS names first, then last known IP if it is still on this subnet, then broadcast. */
    {
        static const char *hosts[] = { SERVER_DNS_HOST0, SERVER_DNS_HOST1 };
        for (size_t i = 0; i < sizeof(hosts) / sizeof(hosts[0]); i++) {
            uint32_t addr = 0;
            if (!resolve_hostname4(hosts[i], &addr)) {
                continue;
            }
            if (!ipv4_on_sta_subnet(addr)) {
                ESP_LOGW(TAG, "dns %s is off-subnet — skip", hosts[i]);
                continue;
            }
            remember_http_host(hosts[i]);
            discovery_send(sock, payload, plen, addr, hosts[i]);
        }
    }
    {
        ip4_addr_t known;
        const char *hint = s_server_ip[0] ? s_server_ip : s_server_ip_hint;
        if (hint[0] && ip4addr_aton(hint, &known) && ipv4_on_sta_subnet(known.addr)) {
            discovery_send(sock, payload, plen, known.addr, "last server");
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
        keypad_collect(POLL_MS);
        maybe_send_status();
        char rx[512];
        struct sockaddr_in src = {0};
        socklen_t slen = sizeof(src);
        int n = recvfrom(sock, rx, sizeof(rx) - 1, 0, (struct sockaddr *)&src, &slen);
        if (n < 0) {
            continue;
        }
        rx[n] = '\0';

        char from[16];
        ip4_addr_t from_a = { .addr = src.sin_addr.s_addr };
        ip4addr_ntoa_r(&from_a, from, sizeof(from));

        if (!parse_json_pong(rx, from)) {
            continue;
        }
        s_have_server = true;
        found = true;
        if (s_server_ip[0]) {
            nvs_save_server_ip_hint(s_server_ip);
        }
        ESP_LOGI(TAG, "discovery pong from %s identity=%s ip=%s time_from_disc=%d",
                 from, s_server_identity, s_server_ip, (int)s_time_from_discovery);
        break;
    }
    close(sock);
    if (!found) {
        ESP_LOGW(TAG, "discovery: no pong (Rails UDP %d / broadcast?)", DISCOVERY_PORT);
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
static void ui_clock_refresh(void);
static void ui_users_page_paint(void);
static void ui_therapies_page_paint(void);
static void enter_therapies_mode(void);
static void enter_skins_mode(void);
static void do_therapy_list_key(void);
static int udp_rpc(cJSON *root, char *rx, size_t rx_cap);
static void on_key(char k);

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

    char rx[1024];
    if (udp_rpc(root, rx, sizeof(rx)) < 0) {
        return false;
    }

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

/** Send JSON to the discovered server and wait for one datagram. Returns n or -1. */
static int udp_rpc(cJSON *root, char *rx, size_t rx_cap)
{
    if (!root || !rx || rx_cap < 2) {
        if (root) {
            cJSON_Delete(root);
        }
        return -1;
    }
    if (!s_have_ip || !s_have_server || s_server_ip[0] == '\0') {
        cJSON_Delete(root);
        return -1;
    }
    char *payload = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!payload) {
        return -1;
    }

    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        free(payload);
        return -1;
    }
    struct timeval tv = {
        .tv_sec = 0,
        .tv_usec = POLL_MS * 1000,
    };
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    struct sockaddr_in dest = {0};
    dest.sin_family = AF_INET;
    dest.sin_port = htons(DISCOVERY_PORT);
    if (inet_aton(s_server_ip, &dest.sin_addr) == 0) {
        ESP_LOGW(TAG, "udp_rpc: bad server ip %s", s_server_ip);
        close(sock);
        free(payload);
        return -1;
    }

    ESP_LOGI(TAG, "udp → %s:%d %s", s_server_ip, DISCOVERY_PORT, payload);
    if (sendto(sock, payload, strlen(payload), 0, (struct sockaddr *)&dest, sizeof(dest)) < 0) {
        ESP_LOGW(TAG, "udp sendto errno %d", errno);
        close(sock);
        free(payload);
        return -1;
    }
    free(payload);

    int64_t deadline = esp_timer_get_time() + ((int64_t)DISCOVERY_TIMEOUT_MS * 1000);
    while (esp_timer_get_time() < deadline) {
        keypad_collect(POLL_MS);
        maybe_send_status();
        struct sockaddr_in src = {0};
        socklen_t slen = sizeof(src);
        int n = recvfrom(sock, rx, rx_cap - 1, 0, (struct sockaddr *)&src, &slen);
        if (n >= 0) {
            rx[n] = '\0';
            ESP_LOGI(TAG, "udp ← %s", rx);
            close(sock);
            return n;
        }
    }
    close(sock);
    ESP_LOGW(TAG, "udp_rpc: no reply");
    return -1;
}

static int parse_pick_array(const cJSON *arr, pick_item_t *out, int cap, bool want_skin_flag)
{
    int count = 0;
    const cJSON *item = NULL;
    cJSON_ArrayForEach(item, arr) {
        if (count >= cap) {
            break;
        }
        const cJSON *id = cJSON_GetObjectItemCaseSensitive(item, "id");
        const cJSON *name = cJSON_GetObjectItemCaseSensitive(item, "name");
        if (!cJSON_IsNumber(id) || !cJSON_IsString(name) || !name->valuestring) {
            continue;
        }
        out[count].id = (int)id->valuedouble;
        strncpy(out[count].name, name->valuestring, sizeof(out[count].name) - 1);
        out[count].name[sizeof(out[count].name) - 1] = '\0';
        if (want_skin_flag) {
            const cJSON *need = cJSON_GetObjectItemCaseSensitive(item, "uses_skin_type");
            out[count].uses_skin_type = cJSON_IsTrue(need);
        } else {
            out[count].uses_skin_type = false;
        }
        count++;
    }
    return count;
}

static bool request_therapy_list(void)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return false;
    }
    cJSON_AddNumberToObject(root, "v", DISCOVERY_JSON_V);
    cJSON_AddStringToObject(root, "type", "therapies");
    cJSON_AddStringToObject(root, "identity", s_device_identity);

    char rx[1024];
    if (udp_rpc(root, rx, sizeof(rx)) < 0) {
        return false;
    }
    cJSON *j = cJSON_Parse(rx);
    if (!j) {
        return false;
    }
    const cJSON *type = cJSON_GetObjectItemCaseSensitive(j, "type");
    const cJSON *ther = cJSON_GetObjectItemCaseSensitive(j, "therapies");
    const cJSON *skins = cJSON_GetObjectItemCaseSensitive(j, "skin_types");
    if (!cJSON_IsString(type) || strcasecmp(type->valuestring, "therapies") != 0 ||
        !cJSON_IsArray(ther)) {
        cJSON_Delete(j);
        return false;
    }
    s_therapy_count = parse_pick_array(ther, s_therapies, PICK_LIST_MAX, true);
    s_skin_count = 0;
    if (cJSON_IsArray(skins)) {
        s_skin_count = parse_pick_array(skins, s_skins, PICK_LIST_MAX, false);
    }
    cJSON_Delete(j);
    ESP_LOGI(TAG, "therapies loaded: %d skins: %d", s_therapy_count, s_skin_count);
    return s_therapy_count > 0;
}

/** Apply last-session / step / max / initial from a therapy or assign_therapy JSON. */
static bool apply_therapy_fields(const cJSON *j, int *out_sec, char *name_out, size_t name_cap,
                                 char *message_out, size_t message_cap)
{
    if (!j) {
        return false;
    }
    const cJSON *rec = cJSON_GetObjectItemCaseSensitive(j, "recommended_seconds");
    const cJSON *step = cJSON_GetObjectItemCaseSensitive(j, "step_seconds");
    const cJSON *step_min = cJSON_GetObjectItemCaseSensitive(j, "step_minutes");
    const cJSON *maxj = cJSON_GetObjectItemCaseSensitive(j, "max_seconds");
    const cJSON *initj = cJSON_GetObjectItemCaseSensitive(j, "initial_seconds");
    const cJSON *last_dur = cJSON_GetObjectItemCaseSensitive(j, "last_duration_seconds");
    const cJSON *tid = cJSON_GetObjectItemCaseSensitive(j, "therapy_id");
    const cJSON *sid = cJSON_GetObjectItemCaseSensitive(j, "skin_id");
    const cJSON *name = cJSON_GetObjectItemCaseSensitive(j, "name");
    const cJSON *message = cJSON_GetObjectItemCaseSensitive(j, "message");

    if (message_out && message_cap > 0 && cJSON_IsString(message) && message->valuestring) {
        strncpy(message_out, message->valuestring, message_cap - 1);
        message_out[message_cap - 1] = '\0';
    }
    if (!cJSON_IsNumber(rec) || rec->valuedouble < 0) {
        return false;
    }
    int sec = (int)rec->valuedouble;
    if (sec > MAX_SESSION_SEC) {
        sec = MAX_SESSION_SEC;
    }
    s_recommended_seconds = sec;
    s_have_recommended = true;
    if (out_sec) {
        *out_sec = sec;
    }
    if (name_out && name_cap > 0 && cJSON_IsString(name) && name->valuestring) {
        strncpy(name_out, name->valuestring, name_cap - 1);
        name_out[name_cap - 1] = '\0';
    }

    if (cJSON_IsNumber(step) && step->valuedouble >= 0) {
        int step_sec = (int)step->valuedouble;
        if (step_sec > MAX_SESSION_SEC) {
            step_sec = MAX_SESSION_SEC;
        }
        s_step_seconds = step_sec;
    } else if (cJSON_IsNumber(step_min) && step_min->valuedouble >= 0) {
        int step_sec = (int)step_min->valuedouble * 60;
        if (step_sec > MAX_SESSION_SEC) {
            step_sec = MAX_SESSION_SEC;
        }
        s_step_seconds = step_sec;
    } else {
        s_step_seconds = DEFAULT_STEP_SECONDS;
    }
    if (cJSON_IsNumber(maxj) && maxj->valuedouble > 0) {
        int mx = (int)maxj->valuedouble;
        if (mx > MAX_SESSION_SEC) {
            mx = MAX_SESSION_SEC;
        }
        s_max_seconds = mx;
    } else {
        s_max_seconds = DEFAULT_MAX_SECONDS;
    }
    if (cJSON_IsNumber(initj) && initj->valuedouble > 0) {
        int init_sec = (int)initj->valuedouble;
        if (init_sec > MAX_SESSION_SEC) {
            init_sec = MAX_SESSION_SEC;
        }
        s_initial_seconds = init_sec;
    } else {
        s_initial_seconds = DEFAULT_INITIAL_SECONDS;
    }
    if (cJSON_IsNumber(last_dur) && last_dur->valuedouble >= 0) {
        int last_sec = (int)last_dur->valuedouble;
        if (last_sec > MAX_SESSION_SEC) {
            last_sec = MAX_SESSION_SEC;
        }
        s_last_duration_sec = last_sec;
        s_have_last_duration = true;
    } else {
        s_last_duration_sec = 0;
        s_have_last_duration = false;
    }
    if (cJSON_IsNumber(tid) && tid->valuedouble >= 1 && tid->valuedouble <= 9) {
        s_therapy_id = (int)tid->valuedouble;
    } else {
        s_therapy_id = 0;
    }
    if (cJSON_IsNumber(sid) && sid->valuedouble >= 1 && sid->valuedouble <= 6) {
        s_skin_id = (int)sid->valuedouble;
    } else {
        s_skin_id = 0;
    }
    if (s_have_recommended && s_recommended_seconds > s_max_seconds) {
        s_recommended_seconds = s_max_seconds;
        if (out_sec) {
            *out_sec = s_max_seconds;
        }
    }
    return true;
}

static bool request_assign_therapy(int user_id, int therapy_id, int skin_id,
                                   int *out_sec, char *name_out, size_t name_cap,
                                   char *message_out, size_t message_cap,
                                   bool *applied_last_session)
{
    if (applied_last_session) {
        *applied_last_session = false;
    }
    if (out_sec) {
        *out_sec = 0;
    }
    if (name_out && name_cap > 0) {
        name_out[0] = '\0';
    }
    if (message_out && message_cap > 0) {
        message_out[0] = '\0';
    }

    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return false;
    }
    cJSON_AddNumberToObject(root, "v", DISCOVERY_JSON_V);
    cJSON_AddStringToObject(root, "type", "assign_therapy");
    cJSON_AddStringToObject(root, "identity", s_device_identity);
    cJSON_AddNumberToObject(root, "user_id", user_id);
    cJSON_AddNumberToObject(root, "therapy_id", therapy_id);
    if (skin_id > 0) {
        cJSON_AddNumberToObject(root, "skin_id", skin_id);
    }

    char rx[1024];
    if (udp_rpc(root, rx, sizeof(rx)) < 0) {
        return false;
    }
    cJSON *j = cJSON_Parse(rx);
    if (!j) {
        return false;
    }
    const cJSON *type = cJSON_GetObjectItemCaseSensitive(j, "type");
    const cJSON *ok = cJSON_GetObjectItemCaseSensitive(j, "ok");
    const cJSON *err = cJSON_GetObjectItemCaseSensitive(j, "error");
    bool good = cJSON_IsString(type) && type->valuestring &&
                strcasecmp(type->valuestring, "assign_therapy") == 0 &&
                cJSON_IsTrue(ok);
    if (!good && cJSON_IsString(err) && err->valuestring) {
        ESP_LOGW(TAG, "assign_therapy error: %s", err->valuestring);
    }
    if (good && apply_therapy_fields(j, out_sec, name_out, name_cap, message_out, message_cap)) {
        if (applied_last_session) {
            *applied_last_session = true;
        }
    }
    cJSON_Delete(j);
    return good;
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
 * On success sets *out_sec, optional name, and optional message buffers; returns true.
 * Also stores recommended / step / max / initial and last_duration for C, D, and *.
 * message_out receives therapy reply "message" when present (may be set on error too).
 */
static bool request_therapy(int user_id, int *out_sec, char *name_out, size_t name_cap,
                            char *message_out, size_t message_cap)
{
    if (out_sec) {
        *out_sec = 0;
    }
    if (name_out && name_cap > 0) {
        name_out[0] = '\0';
    }
    if (message_out && message_cap > 0) {
        message_out[0] = '\0';
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

    char rx[512];
    if (udp_rpc(root, rx, sizeof(rx)) < 0) {
        return false;
    }

    cJSON *j = cJSON_Parse(rx);
    if (!j) {
        return false;
    }
    const cJSON *type = cJSON_GetObjectItemCaseSensitive(j, "type");
    const cJSON *err = cJSON_GetObjectItemCaseSensitive(j, "error");
    if (!cJSON_IsString(type) || strcasecmp(type->valuestring, "therapy") != 0) {
        cJSON_Delete(j);
        return false;
    }
    if (cJSON_IsString(err) && err->valuestring && err->valuestring[0] != '\0') {
        const cJSON *message = cJSON_GetObjectItemCaseSensitive(j, "message");
        if (message_out && message_cap > 0 && cJSON_IsString(message) && message->valuestring) {
            strncpy(message_out, message->valuestring, message_cap - 1);
            message_out[message_cap - 1] = '\0';
        }
        ESP_LOGW(TAG, "therapy error: %s", err->valuestring);
        cJSON_Delete(j);
        return false;
    }
    bool applied = apply_therapy_fields(j, out_sec, name_out, name_cap, message_out, message_cap);
    cJSON_Delete(j);
    return applied;
}

/**
 * Log completed/aborted light-on interval to Rails (UDP exposure).
 * user_id 0 = Guest. duration_seconds = actual lamp-on time.
 * unix = current wall clock at light-off (end time).
 * therapy_id / skin_id are keypad ids captured at lamp-on (0 omitted).
 */
static void report_exposure_log(int user_id, int duration_sec, int therapy_id, int skin_id)
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
    if (therapy_id >= 1 && therapy_id <= 9) {
        cJSON_AddNumberToObject(root, "therapy_id", therapy_id);
    }
    if (skin_id >= 1 && skin_id <= 6) {
        cJSON_AddNumberToObject(root, "skin_id", skin_id);
    }
    if (s_test_flag) {
        cJSON_AddTrueToObject(root, "test");
    }
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
    sec = clamp_session_sec(sec);
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
    /* LCD painted by show_therapy_message_if_any (or caller). */
}

/**
 * Therapy message hold, then entry top line with last-session bottom sticky.
 */
static void capture_last_session_bottom(const char *message)
{
    s_have_last_session_bottom = false;
    s_last_session_bottom[0] = '\0';
    if (!message || message[0] == '\0') {
        return;
    }
    const char *nl = strchr(message, '\n');
    const char *detail = nl ? (nl + 1) : message;
    while (*detail == ' ' || *detail == '\t' || *detail == '\r') {
        detail++;
    }
    if (detail[0] == '\0') {
        return;
    }
    if (!nl && strcasecmp(detail, "No prior session") == 0) {
        return;
    }
    snprintf(s_last_session_bottom, sizeof(s_last_session_bottom), "%.16s", detail);
    s_have_last_session_bottom = true;
}

static void ui_entry_paint_top_keep_bottom(void)
{
    int mm, ss;
    entry_to_mmss(s_entry, &mm, &ss);
    int sec_show = ss > 59 ? 59 : ss;
    show_led_mmss(mm, sec_show, true);

    char l0[17], l1[17];
    format_user_time_line(l0, sizeof(l0), mm, sec_show);
    if (s_have_last_session_bottom && s_last_session_bottom[0] != '\0') {
        snprintf(l1, sizeof(l1), "%.16s", s_last_session_bottom);
    } else if (s_after_complete && (s_entry_digits > 0 || s_entry > 0)) {
        snprintf(l1, sizeof(l1), "* clear repeat #");
    } else {
        snprintf(l1, sizeof(l1), "* clear  start #");
    }
    lcd_status(l0, l1);
}

static void show_therapy_message_if_any(const char *message)
{
    capture_last_session_bottom(message);
    if (!message || message[0] == '\0') {
        if (s_state == ST_ENTRY) {
            ui_entry_paint_top_keep_bottom();
        }
        return;
    }
    ESP_LOGI(TAG, "therapy message: %s", message);
    lcd_show_message(message);
    note_input();
    hold_collecting_keys(THERAPY_MSG_HOLD_MS);
    note_input();
    if (s_state == ST_ENTRY) {
        ui_entry_paint_top_keep_bottom();
    }
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

    /* A1B4: next key is B, so just select the user and let B assign. */
    char next = '\0';
    if (key_q_peek(&next) && next == 'B') {
        s_selected_user_id = user_id;
        strncpy(s_selected_user_name, name_buf, sizeof(s_selected_user_name) - 1);
        s_selected_user_name[sizeof(s_selected_user_name) - 1] = '\0';
        s_state = ST_ENTRY;
        note_input();
        ESP_LOGI(TAG, "user %d selected; B queued — skip therapy load", user_id);
        return;
    }

    int sec = 0;
    char reply_name[24];
    char message_buf[THERAPY_MSG_CAP];
    if (!request_therapy(user_id, &sec, reply_name, sizeof(reply_name),
                         message_buf, sizeof(message_buf))) {
        if (message_buf[0] != '\0') {
            lcd_show_message(message_buf);
        } else {
            lcd_status("Therapy fail", name_buf);
        }
        note_input();
        /* Stay in users mode so they can try another digit or wait out. */
        s_state = ST_USERS;
        s_users_page_start_us = esp_timer_get_time();
        hold_collecting_keys(message_buf[0] ? THERAPY_MSG_HOLD_MS : 800);
        ui_users_page_paint();
        return;
    }
    if (reply_name[0] != '\0') {
        strncpy(name_buf, reply_name, sizeof(name_buf) - 1);
        name_buf[sizeof(name_buf) - 1] = '\0';
    }
    apply_therapy_entry(user_id, name_buf, sec);
    show_therapy_message_if_any(message_buf);
}

static int find_pick_index_by_id(const pick_item_t *items, int count, int id)
{
    for (int i = 0; i < count; i++) {
        if (items[i].id == id) {
            return i;
        }
    }
    return -1;
}

static void reload_therapy_after_assign(void)
{
    int user_id = s_selected_user_id;
    if (user_id < 0) {
        user_id = 0;
    }
    char name_buf[24];
    snprintf(name_buf, sizeof(name_buf), "%s", selected_user_label());
    lcd_status(name_buf, "therapy…");
    int sec = 0;
    char reply_name[24];
    char message_buf[THERAPY_MSG_CAP];
    if (!request_therapy(user_id, &sec, reply_name, sizeof(reply_name),
                         message_buf, sizeof(message_buf))) {
        lcd_status("Therapy fail", name_buf);
        s_state = ST_ENTRY;
        note_input();
        return;
    }
    if (reply_name[0] != '\0') {
        strncpy(name_buf, reply_name, sizeof(name_buf) - 1);
        name_buf[sizeof(name_buf) - 1] = '\0';
    }
    apply_therapy_entry(user_id, name_buf, sec);
    show_therapy_message_if_any(message_buf);
}

static void assign_pending_therapy(int skin_id)
{
    int user_id = s_selected_user_id;
    if (user_id < 0) {
        user_id = 0;
    }
    lcd_status(s_pending_therapy_name[0] ? s_pending_therapy_name : "Therapy",
               "assign…");
    int sec = 0;
    char reply_name[24];
    char message_buf[THERAPY_MSG_CAP];
    bool applied = false;
    if (!request_assign_therapy(user_id, s_pending_therapy_id, skin_id,
                                &sec, reply_name, sizeof(reply_name),
                                message_buf, sizeof(message_buf), &applied)) {
        lcd_status("Assign fail", "try again");
        note_input();
        s_state = ST_THERAPIES;
        s_users_page_start_us = esp_timer_get_time();
        hold_collecting_keys(800);
        ui_therapies_page_paint();
        return;
    }
    /* Same last-session LCD as A+digit: last lamp-on still counts after the mode change. */
    if (applied) {
        char name_buf[24];
        snprintf(name_buf, sizeof(name_buf), "%s",
                 reply_name[0] ? reply_name : selected_user_label());
        apply_therapy_entry(user_id, name_buf, sec);
        show_therapy_message_if_any(message_buf);
        return;
    }
    reload_therapy_after_assign();
}

static void select_therapy_by_digit(char digit)
{
    int id = digit - '0';
    int idx = find_pick_index_by_id(s_therapies, s_therapy_count, id);
    if (idx < 0) {
        lcd_status("Unknown id", "try 1-4");
        note_input();
        ESP_LOGW(TAG, "digit %c: therapy_id %d not in list", digit, id);
        return;
    }
    s_pending_therapy_id = s_therapies[idx].id;
    strncpy(s_pending_therapy_name, s_therapies[idx].name, sizeof(s_pending_therapy_name) - 1);
    s_pending_therapy_name[sizeof(s_pending_therapy_name) - 1] = '\0';
    if (s_therapies[idx].uses_skin_type) {
        if (s_skin_count <= 0) {
            lcd_status("No skin types", "on server");
            note_input();
            return;
        }
        enter_skins_mode();
        return;
    }
    assign_pending_therapy(0);
}

static void select_skin_by_digit(char digit)
{
    int id = digit - '0';
    int idx = find_pick_index_by_id(s_skins, s_skin_count, id);
    if (idx < 0) {
        lcd_status("Unknown id", "try 1-6");
        note_input();
        ESP_LOGW(TAG, "digit %c: skin_id %d not in list", digit, id);
        return;
    }
    assign_pending_therapy(s_skins[idx].id);
}

static void do_therapy_list_key(void)
{
    note_input();
    if (!s_have_server || s_server_ip[0] == '\0') {
        lcd_status("Therapy…", "discover");
        ESP_LOGI(TAG, "key B: no server — retry discovery");
        if (s_have_ip) {
            s_need_discovery = true;
            run_discovery("key B");
        }
    }

    lcd_status("Therapy…", s_have_server ? s_server_ip : "no server");
    if (request_therapy_list()) {
        enter_therapies_mode();
    } else {
        lcd_status("Therapy fail", s_have_server ? "timeout" : "no server");
        s_state = ST_ENTRY;
        note_input();
    }
}

/** C adds step_seconds to recommended; D subtracts (floor 0).
 *  Happy path: server recommended_seconds is last duration. Too soon (0) stays 0.
 *  Before any therapy reply, fall back to last duration (usually 0). */
static void apply_last_step(int sign)
{
    int base;
    if (s_have_recommended) {
        if (s_recommended_seconds <= 0) {
            apply_therapy_entry(s_selected_user_id, selected_user_label(), 0);
            ui_entry_paint_top_keep_bottom();
            ESP_LOGI(TAG, "%c ignored: recommended 0", sign >= 0 ? 'C' : 'D');
            return;
        }
        base = s_recommended_seconds;
    } else {
        base = s_have_last_duration ? s_last_duration_sec : 0;
    }
    int step = s_step_seconds;
    if (step < 0) {
        step = DEFAULT_STEP_SECONDS;
    }
    if (step > MAX_SESSION_SEC) {
        step = MAX_SESSION_SEC;
    }
    if (sign >= 0) {
        sign = 1;
    } else {
        sign = -1;
    }
    int sec = clamp_session_sec(base + (sign * step));
    if (sec < 1 && sign > 0) {
        lcd_status("Need rec+step", "no recommend");
        note_input();
        s_state = ST_ENTRY;
        ESP_LOGW(TAG, "C ignored: rec=%ds step=%ds", base, step);
        return;
    }
    apply_therapy_entry(s_selected_user_id, selected_user_label(), sec);
    ui_entry_paint_top_keep_bottom();
    ESP_LOGI(TAG, "%c: rec %ds %c %ds → %ds (entry %d)",
             sign > 0 ? 'C' : 'D', base, sign > 0 ? '+' : '-', step, sec, s_entry);
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

static void tick_list_mode(void (*paint)(void))
{
    int64_t now = esp_timer_get_time();
    if (now - s_users_mode_start_us >= (int64_t)USERS_MODE_MS * 1000) {
        ESP_LOGI(TAG, "list mode timeout → clock");
        enter_clock_mode();
        return;
    }
    if (now - s_users_page_start_us >= (int64_t)USERS_PAGE_MS * 1000) {
        if (s_users_page_count < 1) {
            s_users_page_count = 1;
            s_users_page_start[0] = 0;
        }
        s_users_page = (s_users_page + 1) % s_users_page_count;
        s_users_page_start_us = now;
        paint();
    }
}

static void tick_users_mode(void)
{
    if (s_users_page_count < 1) {
        recompute_user_pages();
    }
    tick_list_mode(ui_users_page_paint);
}

static bool format_pick_line(char *line, size_t line_cap, const pick_item_t *items, int count, int idx)
{
    if (line_cap < 2) {
        return false;
    }
    line[0] = '\0';
    if (idx < 0 || idx >= count) {
        return false;
    }
    int id = items[idx].id;
    if (id < 0) {
        id = 0;
    }
    if (id > 99) {
        id = id % 100;
    }
    int tlen = snprintf(line, line_cap, "%d:%s", id, items[idx].name);
    if (tlen < 0) {
        line[0] = '\0';
        return false;
    }
    if ((size_t)tlen > (size_t)LCD_COLS && line_cap > LCD_COLS) {
        line[LCD_COLS] = '\0';
    }
    return true;
}

static void recompute_pick_pages(int count)
{
    s_users_page_count = 0;
    if (count <= 0) {
        s_users_page_count = 1;
        s_users_page_start[0] = 0;
        return;
    }
    for (int i = 0; i < count && s_users_page_count < USERS_PAGE_MAX; i += 2) {
        s_users_page_start[s_users_page_count++] = i;
    }
    if (s_users_page_count < 1) {
        s_users_page_count = 1;
        s_users_page_start[0] = 0;
    }
}

static void ui_pick_page_paint(const pick_item_t *items, int count, const char *empty_title)
{
    if (count <= 0) {
        lcd_status(empty_title ? empty_title : "List", "none");
        return;
    }
    if (s_users_page >= s_users_page_count) {
        s_users_page = 0;
    }
    int idx = s_users_page_start[s_users_page];
    char l0[17], l1[17];
    if (!format_pick_line(l0, sizeof(l0), items, count, idx)) {
        snprintf(l0, sizeof(l0), "%s", empty_title ? empty_title : "List");
    }
    if (!format_pick_line(l1, sizeof(l1), items, count, idx + 1)) {
        l1[0] = '\0';
    }
    lcd_status(l0, l1);
    show_led_wall_clock();
}

static void ui_therapies_page_paint(void)
{
    ui_pick_page_paint(s_therapies, s_therapy_count, "Therapy");
}

static void ui_skins_page_paint(void)
{
    ui_pick_page_paint(s_skins, s_skin_count, "Skin type");
}

static void enter_therapies_mode(void)
{
    recompute_pick_pages(s_therapy_count);
    s_state = ST_THERAPIES;
    s_users_page = 0;
    s_users_mode_start_us = esp_timer_get_time();
    s_users_page_start_us = s_users_mode_start_us;
    note_input();
    ui_therapies_page_paint();
    ESP_LOGI(TAG, "therapies mode: %d types", s_therapy_count);
}

static void enter_skins_mode(void)
{
    recompute_pick_pages(s_skin_count);
    s_state = ST_SKINS;
    s_users_page = 0;
    s_users_mode_start_us = esp_timer_get_time();
    s_users_page_start_us = s_users_mode_start_us;
    note_input();
    ui_skins_page_paint();
    ESP_LOGI(TAG, "skins mode after %s: %d types", s_pending_therapy_name, s_skin_count);
}

static void tick_therapies_mode(void)
{
    if (s_users_page_count < 1) {
        recompute_pick_pages(s_therapy_count);
    }
    tick_list_mode(ui_therapies_page_paint);
}

static void tick_skins_mode(void)
{
    if (s_users_page_count < 1) {
        recompute_pick_pages(s_skin_count);
    }
    tick_list_mode(ui_skins_page_paint);
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

/**
 * OTA safety gate: never flash while a UV session is running.
 * Also skip while paging users / therapies / skins (short interactive UI).
 */
static bool ota_may_start(void)
{
    if (s_state == ST_RUNNING) {
        return false;
    }
    if (s_state == ST_USERS || s_state == ST_THERAPIES || s_state == ST_SKINS) {
        return false;
    }
    return true;
}

static void request_ota_check(const char *reason)
{
    s_ota_requested = true;
    ESP_LOGI(TAG, "OTA requested (%s) running=%s",
             reason ? reason : "?", uh_ota_running_version());
    if (s_state == ST_RUNNING) {
        lcd_status("Update after", "session");
        return;
    }
    if (s_state == ST_USERS || s_state == ST_THERAPIES || s_state == ST_SKINS) {
        s_state = ST_ENTRY;
        note_input();
    }
    lcd_status("Checking for", "update…");
}

static void cmd_sock_ensure(void)
{
    if (s_cmd_sock >= 0 || !s_have_ip) {
        return;
    }
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        return;
    }
    int one = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(DISCOVERY_PORT);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        ESP_LOGW(TAG, "cmd sock bind :%d failed errno %d", DISCOVERY_PORT, errno);
        close(sock);
        return;
    }
    s_cmd_sock = sock;
    ESP_LOGI(TAG, "cmd sock listening UDP :%d (type ota, key)", DISCOVERY_PORT);
}

static char normalize_inject_key(char k)
{
    if (k >= 'a' && k <= 'd') {
        return (char)('A' + (k - 'a'));
    }
    return k;
}

static bool valid_inject_key(char k)
{
    k = normalize_inject_key(k);
    if (k >= '0' && k <= '9') {
        return true;
    }
    if (k >= 'A' && k <= 'D') {
        return true;
    }
    return k == '*' || k == '#';
}

static void cmd_send_json(cJSON *ack, const struct sockaddr_in *src, socklen_t slen)
{
    if (!ack) {
        return;
    }
    char *payload = cJSON_PrintUnformatted(ack);
    cJSON_Delete(ack);
    if (!payload) {
        return;
    }
    sendto(s_cmd_sock, payload, strlen(payload), 0, (const struct sockaddr *)src, slen);
    free(payload);
}

static void cmd_handle_ota(const struct sockaddr_in *src, socklen_t slen)
{
    remember_cmd_peer(src);
    ESP_LOGI(TAG, "ota cmd from %s", inet_ntoa(src->sin_addr));
    request_ota_check("udp");

    cJSON *ack = cJSON_CreateObject();
    if (!ack) {
        return;
    }
    cJSON_AddNumberToObject(ack, "v", DISCOVERY_JSON_V);
    cJSON_AddStringToObject(ack, "type", "ota");
    cJSON_AddTrueToObject(ack, "ok");
    cJSON_AddBoolToObject(ack, "busy", !ota_may_start() && s_state == ST_RUNNING);
    cJSON_AddStringToObject(ack, "version", uh_ota_running_version());
    cJSON_AddStringToObject(ack, "identity", s_device_identity);
    cmd_send_json(ack, src, slen);
}

static void cmd_handle_key(const cJSON *j, const struct sockaddr_in *src, socklen_t slen)
{
    char keys[17];
    int nkeys = 0;
    const cJSON *one = cJSON_GetObjectItemCaseSensitive(j, "key");
    const cJSON *many = cJSON_GetObjectItemCaseSensitive(j, "keys");
    if (cJSON_IsString(many) && many->valuestring) {
        for (const char *p = many->valuestring; *p && nkeys < (int)sizeof(keys) - 1; p++) {
            if (*p == ' ' || *p == ',' || *p == '\t') {
                continue;
            }
            char k = normalize_inject_key(*p);
            if (!valid_inject_key(k)) {
                continue;
            }
            keys[nkeys++] = k;
        }
    } else if (cJSON_IsString(one) && one->valuestring && one->valuestring[0]) {
        char k = normalize_inject_key(one->valuestring[0]);
        if (valid_inject_key(k)) {
            keys[nkeys++] = k;
        }
    } else if (cJSON_IsNumber(one)) {
        int d = (int)one->valuedouble;
        if (d >= 0 && d <= 9) {
            keys[nkeys++] = (char)('0' + d);
        }
    }
    keys[nkeys] = '\0';
    remember_cmd_peer(src);

    if (nkeys < 1) {
        cJSON *ack = cJSON_CreateObject();
        if (!ack) {
            return;
        }
        cJSON_AddNumberToObject(ack, "v", DISCOVERY_JSON_V);
        cJSON_AddStringToObject(ack, "type", "key");
        cJSON_AddFalseToObject(ack, "ok");
        cJSON_AddStringToObject(ack, "error", "bad_key");
        cJSON_AddBoolToObject(ack, "test", s_test_flag);
        cmd_send_json(ack, src, slen);
        return;
    }

    ESP_LOGI(TAG, "key cmd from %s keys=%s", inet_ntoa(src->sin_addr), keys);
    for (int i = 0; i < nkeys; i++) {
        s_test_flag = true;
        mark_status_dirty();
        ESP_LOGI(TAG, "key '%c' test=1 (udp)", keys[i]);
        on_key(keys[i]);
    }

    cJSON *ack = cJSON_CreateObject();
    if (!ack) {
        return;
    }
    cJSON_AddNumberToObject(ack, "v", DISCOVERY_JSON_V);
    cJSON_AddStringToObject(ack, "type", "key");
    cJSON_AddTrueToObject(ack, "ok");
    cJSON_AddStringToObject(ack, "keys", keys);
    cJSON_AddBoolToObject(ack, "test", s_test_flag);
    cJSON_AddStringToObject(ack, "identity", s_device_identity);
    cJSON *st = status_object();
    if (st) {
        cJSON_AddItemToObject(ack, "status", st);
    }
    cmd_send_json(ack, src, slen);
}

static void cmd_handle_status(const struct sockaddr_in *src, socklen_t slen)
{
    remember_cmd_peer(src);
    ESP_LOGI(TAG, "status check from %s", inet_ntoa(src->sin_addr));
    cJSON *ack = cJSON_CreateObject();
    if (!ack) {
        return;
    }
    cJSON_AddNumberToObject(ack, "v", DISCOVERY_JSON_V);
    cJSON_AddStringToObject(ack, "type", "status");
    cJSON_AddTrueToObject(ack, "ok");
    cJSON_AddBoolToObject(ack, "test", s_test_flag);
    cJSON_AddStringToObject(ack, "identity", s_device_identity);
    cJSON *st = status_object();
    if (st) {
        cJSON_AddItemToObject(ack, "status", st);
    }
    cmd_send_json(ack, src, slen);
}

static void cmd_handle_unwatch(const struct sockaddr_in *src, socklen_t slen)
{
    bool ok = forget_cmd_peer(src);
    ESP_LOGI(TAG, "unwatch from %s ok=%d", inet_ntoa(src->sin_addr), (int)ok);
    cJSON *ack = cJSON_CreateObject();
    if (!ack) {
        return;
    }
    cJSON_AddNumberToObject(ack, "v", DISCOVERY_JSON_V);
    cJSON_AddStringToObject(ack, "type", "unwatch");
    if (ok) {
        cJSON_AddTrueToObject(ack, "ok");
    } else {
        cJSON_AddFalseToObject(ack, "ok");
        cJSON_AddStringToObject(ack, "error", "not_watcher");
    }
    cJSON_AddBoolToObject(ack, "watching", s_have_cmd_peer);
    cJSON_AddStringToObject(ack, "identity", s_device_identity);
    cmd_send_json(ack, src, slen);
}

static void cmd_sock_poll(void)
{
    cmd_sock_ensure();
    if (s_cmd_sock < 0) {
        return;
    }
    char rx[256];
    struct sockaddr_in src = {0};
    socklen_t slen = sizeof(src);
    int n = recvfrom(s_cmd_sock, rx, sizeof(rx) - 1, MSG_DONTWAIT,
                     (struct sockaddr *)&src, &slen);
    if (n < 0) {
        return;
    }
    rx[n] = '\0';
    cJSON *j = cJSON_Parse(rx);
    if (!j) {
        return;
    }
    const cJSON *type = cJSON_GetObjectItemCaseSensitive(j, "type");
    const cJSON *ok = cJSON_GetObjectItemCaseSensitive(j, "ok");
    bool is_ack = cJSON_IsTrue(ok) || cJSON_IsFalse(ok);
    const char *t = (cJSON_IsString(type) && type->valuestring) ? type->valuestring : "";
    if (strcasecmp(t, "ota") == 0 && !is_ack) {
        cJSON_Delete(j);
        cmd_handle_ota(&src, slen);
        return;
    }
    if (strcasecmp(t, "key") == 0 && !is_ack) {
        cmd_handle_key(j, &src, slen);
        cJSON_Delete(j);
        return;
    }
    if ((strcasecmp(t, "status") == 0 || strcasecmp(t, "check") == 0 ||
         strcasecmp(t, "watch") == 0) && !is_ack) {
        /* Host poll / start watching (no nested status). Outgoing module
         * status also has type "status" plus a status object. */
        const cJSON *st = cJSON_GetObjectItemCaseSensitive(j, "status");
        if (!cJSON_IsObject(st)) {
            cmd_handle_status(&src, slen);
        }
        cJSON_Delete(j);
        return;
    }
    if ((strcasecmp(t, "unwatch") == 0 || strcasecmp(t, "stop") == 0) && !is_ack) {
        cJSON_Delete(j);
        cmd_handle_unwatch(&src, slen);
        return;
    }
    cJSON_Delete(j);
}

/**
 * Poll LAN server for a newer session_timer image (HTTP + SHA-256).
 * Blocks during download; only called when idle. Successful OTA reboots.
 */
static void maybe_ota_check(void)
{
    if (!s_have_ip) {
        return;
    }

    const char *host = NULL;
    if (s_server_http_host[0] != '\0') {
        host = s_server_http_host;
    } else if (s_have_server && ipv4_str_on_sta_subnet(s_server_ip)) {
        host = s_server_ip;
    } else if (ipv4_str_on_sta_subnet(s_server_ip_hint)) {
        host = s_server_ip_hint;
    }
    if (!host) {
        return;
    }

    int64_t now = esp_timer_get_time();
    bool requested = s_ota_requested;
    if (!s_ota_boot_marked) {
        /* Cancel bootloader rollback, then fall through into the first check. */
        if (!requested && now < (int64_t)OTA_BOOT_DELAY_MS * 1000) {
            return;
        }
        uh_ota_mark_valid();
        s_ota_boot_marked = true;
        ESP_LOGI(TAG, "OTA: app marked valid; version=%s", uh_ota_running_version());
    } else if (!requested && (now - s_last_ota_check_us) < (int64_t)OTA_CHECK_PERIOD_MS * 1000) {
        return;
    }
    if (!ota_may_start()) {
        ESP_LOGD(TAG, "OTA: deferred (busy UI/session)");
        return;
    }
    s_ota_requested = false;

    s_last_ota_check_us = now;

    char base[64];
    snprintf(base, sizeof(base), "http://%s", host);

    uh_ota_config_t cfg = {
        .base_url = base,
        .app_name = "session_timer",
        .may_start = ota_may_start,
        .skip_if_same_version = true,
    };

    ESP_LOGI(TAG, "OTA check %s/firmware/session_timer/ (running %s)",
             base, uh_ota_running_version());
    lcd_status("Checking for", "update…");
    esp_err_t err = uh_ota_check_and_update(&cfg);
    /* uh_ota reboots on success; these paths are skip / no image / error. */
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "OTA: up to date");
        if (requested) {
            lcd_status("Up to date", uh_ota_running_version());
            vTaskDelay(pdMS_TO_TICKS(1200));
        }
    } else if (err == ESP_ERR_NOT_FOUND) {
        ESP_LOGI(TAG, "OTA: no manifest/image on server");
        if (requested) {
            lcd_status("No update", "on server");
            vTaskDelay(pdMS_TO_TICKS(1200));
        }
    } else if (err == ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "OTA: skipped (not safe to update now)");
    } else {
        ESP_LOGW(TAG, "OTA: %s", esp_err_to_name(err));
        if (requested) {
            lcd_status("Update fail", esp_err_to_name(err));
            vTaskDelay(pdMS_TO_TICKS(1200));
        }
    }

    if (s_state == ST_CLOCK) {
        ui_clock_refresh();
    } else if (s_state == ST_ENTRY) {
        ui_entry_refresh();
    }
}

/* ---------- UI modes ---------- */

static void ui_entry_refresh(void)
{
    ui_entry_paint_top_keep_bottom();
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
    /* * left — matches keypad; full 16 cols */
    if (s_warmup_until_us > 0) {
        snprintf(l1, sizeof(l1), "* abort  Warming");
    } else {
        snprintf(l1, sizeof(l1), "* abort  Running");
    }
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

    /* Bottom: calendar date + A/P (AM/PM) in last column, or offline note. */
    if (valid) {
        /* "YYYY-MM-DD Www" is 14 cols; pad so A/P is column 16 */
        char dbuf[16];
        strftime(dbuf, sizeof(dbuf), "%Y-%m-%d %a", &t);
        snprintf(l1, sizeof(l1), "%-15s%c", dbuf, (t.tm_hour < 12) ? 'A' : 'P');
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
    /* * restores the last therapy initial dose (30 s until a reply); keep user. */
    int sec = clamp_session_sec(s_initial_seconds);
    s_entry = seconds_to_entry(sec);
    s_entry_digits = entry_digit_count(s_entry);
    if (s_entry_digits < 1 && s_entry > 0) {
        s_entry_digits = 1;
    }
    if (s_entry_digits < 1) {
        s_entry_digits = DEFAULT_ENTRY_DIGITS;
    }
    s_entry_fresh = true;
    s_after_complete = false;
    s_have_last_session_bottom = false;
    s_last_session_bottom[0] = '\0';
}

static void start_session(void)
{
    int total = entry_to_seconds(s_entry);
    int capped = clamp_session_sec(total);
    if (capped < total) {
        ESP_LOGW(TAG, "start capped %ds → %ds (max)", total, capped);
        total = capped;
        s_entry = seconds_to_entry(total);
        lcd_status("Capped at max", selected_user_label());
    }
    if (total <= 0) {
        lcd_status("Need time > 0", "digits then #");
        ESP_LOGW(TAG, "start ignored: zero");
        note_input();
        return;
    }
    s_have_last_session_bottom = false;
    s_last_session_bottom[0] = '\0';
    s_remain_sec = total;
    s_session_planned_sec = total;
    s_session_user_id = s_selected_user_id > 0 ? s_selected_user_id : 0;
    s_session_therapy_id = s_therapy_id;
    s_session_skin_id = s_skin_id;
    s_last_tick_us = esp_timer_get_time();
    s_warmup_until_us = s_last_tick_us + (int64_t)LAMP_WARMUP_MS * 1000;
    s_state = ST_RUNNING;
    s_entry_fresh = true;
    s_after_complete = false;
    note_input();
    lamp_set(true);
    ui_running_refresh();
    ESP_LOGI(TAG, "start %d s + %d ms warmup (entry %d) user_id=%d",
             total, LAMP_WARMUP_MS, s_entry, s_session_user_id);
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
    report_exposure_log(uid, elapsed, s_session_therapy_id, s_session_skin_id);
    if (elapsed >= 1) {
        s_last_duration_sec = elapsed;
        s_have_last_duration = true;
    }
    s_warmup_until_us = 0;
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
        report_exposure_log(uid, elapsed, s_session_therapy_id, s_session_skin_id);
        s_last_duration_sec = elapsed;
        s_have_last_duration = true;
    }
    s_warmup_until_us = 0;
    s_state = ST_ENTRY;
    s_entry_fresh = true;
    s_after_complete = false;
    note_input();
    ui_entry_refresh();
    ESP_LOGI(TAG, "aborted — sticky entry %d logged %ds user=%d", s_entry, elapsed, uid);
}

static void on_key(char k)
{
    /* Users paging: digit selects user (therapy); A refreshes; B → therapy list. */
    if (s_state == ST_USERS) {
        if (k == 'A') {
            do_user_list_key();
            return;
        }
        if (k == 'B') {
            do_therapy_list_key();
            return;
        }
        if (k >= '0' && k <= '9') {
            select_user_by_digit(k);
            return;
        }
        s_state = ST_ENTRY;
        note_input();
        if (k == 'C') {
            apply_last_step(1);
            return;
        }
        if (k == 'D') {
            apply_last_step(-1);
            return;
        }
        /* Fall through for * / # */
    }

    /* Therapy paging: digit selects type; B refreshes; A → users. */
    if (s_state == ST_THERAPIES) {
        if (k == 'B') {
            do_therapy_list_key();
            return;
        }
        if (k == 'A') {
            do_user_list_key();
            return;
        }
        if (k >= '0' && k <= '9') {
            select_therapy_by_digit(k);
            return;
        }
        s_state = ST_ENTRY;
        note_input();
        if (k == 'C') {
            apply_last_step(1);
            return;
        }
        if (k == 'D') {
            apply_last_step(-1);
            return;
        }
        /* Fall through for * / # */
    }

    /* Skin paging: digit selects type; B back to therapies. */
    if (s_state == ST_SKINS) {
        if (k == 'B') {
            enter_therapies_mode();
            return;
        }
        if (k == 'A') {
            do_user_list_key();
            return;
        }
        if (k >= '0' && k <= '9') {
            select_skin_by_digit(k);
            return;
        }
        s_state = ST_ENTRY;
        note_input();
        if (k == 'C') {
            apply_last_step(1);
            return;
        }
        if (k == 'D') {
            apply_last_step(-1);
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
        if (k == 'C') {
            apply_last_step(1);
            return;
        }
        if (k == 'D') {
            apply_last_step(-1);
            return;
        }
        if (k == 'B') {
            do_therapy_list_key();
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
    if (k == 'C') {
        apply_last_step(1);
        return;
    }
    if (k == 'D') {
        apply_last_step(-1);
        return;
    }
    if (k == 'B') {
        do_therapy_list_key();
        return;
    }
}

static void tick_running(void)
{
    int64_t now = esp_timer_get_time();
    if (s_warmup_until_us > 0) {
        int mm, ss;
        remain_to_mmss(s_remain_sec, &mm, &ss);
        bool colon = ((now / 500000LL) % 2) == 0;
        show_led_mmss(mm, ss, colon);
        if (now < s_warmup_until_us) {
            return;
        }
        s_warmup_until_us = 0;
        s_last_tick_us = now;
        ui_running_refresh();
        mark_status_dirty();
        ESP_LOGI(TAG, "warmup done — counting %d s", s_remain_sec);
        return;
    }
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
    if (s_state == ST_RUNNING || s_state == ST_CLOCK || s_state == ST_USERS ||
        s_state == ST_THERAPIES || s_state == ST_SKINS) {
        return;
    }
    int64_t idle_us = esp_timer_get_time() - s_last_input_us;
    if (idle_us >= (int64_t)IDLE_TO_CLOCK_MS * 1000) {
        enter_clock_mode();
    }
}

/* ---------- init ---------- */

/**
 * Probe optional front-panel hardware. Missing LCD / TM1637 / keypad is OK
 * (bench ESP without UI still runs Wi‑Fi + UDP discovery).
 */
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
        ESP_LOGW(TAG, "I2C bus init failed — continuing without LCD/keypad");
        bus = NULL;
    }

    s_lcd_ok = false;
    s_tm_ok = false;
    s_kp_ok = false;

    if (bus) {
        uint8_t lcd_addr = 0;
        if (lcd1602_init(bus, &s_lcd, &lcd_addr) == ESP_OK) {
            lcd1602_backlight(&s_lcd, true);
            lcd1602_clear(&s_lcd);
            s_lcd_ok = true;
            ESP_LOGI(TAG, "LCD present @ 0x%02x", lcd_addr);
        } else {
            ESP_LOGW(TAG, "LCD not present — status lines on serial only");
        }

        if (keypad_i2c_init(bus, &s_kp, KEYPAD_ADDR) == ESP_OK) {
            s_kp_ok = true;
            ESP_LOGI(TAG, "keypad present @ 0x%02x", KEYPAD_ADDR);
        } else {
            ESP_LOGW(TAG, "keypad not present — keys disabled");
        }
    }

    if (tm1637_init(&s_tm, TM_CLK_GPIO, TM_DIO_GPIO) == ESP_OK) {
        tm1637_set_brightness(&s_tm, 5);
        s_tm_ok = true;
        ESP_LOGI(TAG, "TM1637 present");
    } else {
        ESP_LOGW(TAG, "TM1637 not present — LED digits disabled");
    }

    ESP_LOGI(TAG, "peripherals: lcd=%d tm=%d keypad=%d",
             (int)s_lcd_ok, (int)s_tm_ok, (int)s_kp_ok);
    return true;
}

void app_main(void)
{
    ESP_LOGI(TAG, "session_timer lamp=%d fan=%d LED=%d piezo=%d version=%s",
             SSR_GPIO, FAN_SSR_GPIO, LAMP_LED_GPIO, PIEZO_GPIO,
             uh_ota_running_version());

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
    nvs_load_server_ip_hint();

    lcd_status("Session timer", "WiFi…");
    if (wifi_start_from_nvs()) {
        xTaskCreate(discovery_task, "udp_disc", 8192, NULL, 4, NULL);
        if (wait_for_ip_brief()) {
            /* Prefer wall clock from Rails discovery pong; SNTP only if that fails. */
            s_need_discovery = true;
            int waited = 0;
            while (waited < DISCOVERY_BOOT_WAIT_MS && !s_have_server) {
                vTaskDelay(pdMS_TO_TICKS(200));
                waited += 200;
            }
            if (s_have_server) {
                ESP_LOGI(TAG, "boot discovery ok: %s @ %s", s_server_identity, s_server_ip);
                lcd_status("Server found", s_server_ip[0] ? s_server_ip : s_server_identity);
            } else {
                ESP_LOGW(TAG, "no discovery yet — SNTP fallback if needed");
                lcd_status("Session timer", "SNTP…");
            }
            if (!s_time_from_discovery) {
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

    int64_t last_clock_paint_us = 0;
    int64_t last_headless_log_us = 0;

    while (1) {
        /* Keypad first so Wi‑Fi / LCD paint cannot starve input (skip if absent). */
        keypad_collect(POLL_MS);
        char pending;
        while (key_q_pop(&pending)) {
            ESP_LOGI(TAG, "key '%c' state=%d", pending, (int)s_state);
            on_key(pending);
        }

        network_maintenance();
        tick_fan_rundown();
        maybe_send_status();
        cmd_sock_poll();
        maybe_ota_check();

        /* Headless bench: periodic status (no keypad/LCD). */
        if (!s_kp_ok && !s_lcd_ok) {
            int64_t now = esp_timer_get_time();
            if (now - last_headless_log_us >= 30000000LL) {
                last_headless_log_us = now;
                ESP_LOGI(TAG,
                         "headless: ip=%d server=%s@%s time_from_disc=%d",
                         (int)s_have_ip,
                         s_have_server ? s_server_identity : "-",
                         s_have_server ? s_server_ip : "-",
                         (int)s_time_from_discovery);
            }
        }

        if (s_state == ST_RUNNING) {
            tick_running();
        } else if (s_state == ST_USERS) {
            tick_users_mode();
        } else if (s_state == ST_THERAPIES) {
            tick_therapies_mode();
        } else if (s_state == ST_SKINS) {
            tick_skins_mode();
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
