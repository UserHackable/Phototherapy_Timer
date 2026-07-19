/*
 * ESP-IDF app: keypad_hello
 *
 * Bring-up for 4×4 membrane + PCF8574 I²C adapter (this bench: 0x20).
 * Shows each key on the LCD1602 and UART. Debounced edge detection.
 *
 * Wiring:
 *   I²C SDA=GPIO21 (orange), SCL=GPIO22 (yellow) — shared with LCD
 *   Keypad adapter @ 0x20, LCD backpack @ 0x27
 *
 *   ./scripts/fw idf upload keypad_hello
 *   ./scripts/fw idf monitor keypad_hello
 */

#include <stdio.h>
#include <string.h>

#include "driver/i2c_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "keypad_i2c.h"
#include "lcd1602_pcf8574.h"

static const char *TAG = "keypad_hello";

#define I2C_SDA_GPIO 21
#define I2C_SCL_GPIO 22
#define KEYPAD_ADDR  0x20

#define POLL_MS      20
#define DEBOUNCE_MS  40

void app_main(void)
{
    ESP_LOGI(TAG, "keypad_hello SDA=%d SCL=%d keypad=0x%02x",
             I2C_SDA_GPIO, I2C_SCL_GPIO, KEYPAD_ADDR);

    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = I2C_SDA_GPIO,
        .scl_io_num = I2C_SCL_GPIO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };

    i2c_master_bus_handle_t bus = NULL;
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_cfg, &bus));

    lcd1602_t lcd;
    bool lcd_ok = false;
    uint8_t lcd_addr = 0;
    if (lcd1602_init(bus, &lcd, &lcd_addr) == ESP_OK) {
        lcd1602_backlight(&lcd, true);
        lcd1602_clear(&lcd);
        lcd1602_print_line(&lcd, 0, "Keypad hello");
        lcd1602_print_line(&lcd, 1, "Press a key...");
        lcd_ok = true;
        ESP_LOGI(TAG, "LCD at 0x%02x", lcd_addr);
    } else {
        ESP_LOGW(TAG, "LCD init failed — serial only");
    }

    keypad_i2c_t kp;
    ESP_ERROR_CHECK(keypad_i2c_init(bus, &kp, KEYPAD_ADDR));

    char pending = '\0';
    int pending_ms = 0;
    bool armed = true; /* require release between reports */
    int press_count = 0;

    ESP_LOGI(TAG, "scanning — press keys on the 4x4 (map calibrated)");

    while (1) {
        char key = '\0';
        bool down = keypad_i2c_scan(&kp, &key);

        if (down) {
            if (key == pending) {
                pending_ms += POLL_MS;
            } else {
                pending = key;
                pending_ms = 0;
            }

            if (armed && pending_ms >= DEBOUNCE_MS) {
                armed = false;
                press_count++;
                ESP_LOGI(TAG, "key '%c'  (#%d)", pending, press_count);

                if (lcd_ok) {
                    char line0[24];
                    char line1[24];
                    snprintf(line0, sizeof(line0), "Key: %c", pending);
                    snprintf(line1, sizeof(line1), "Count: %d", press_count);
                    lcd1602_print_line(&lcd, 0, line0);
                    lcd1602_print_line(&lcd, 1, line1);
                }
            }
        } else {
            pending = '\0';
            pending_ms = 0;
            armed = true;
        }

        vTaskDelay(pdMS_TO_TICKS(POLL_MS));
    }
}
