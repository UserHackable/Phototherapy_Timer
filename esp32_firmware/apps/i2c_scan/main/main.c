/*
 * ESP-IDF app: i2c_scan
 *
 * Probe the default project I²C bus (SDA=GPIO21, SCL=GPIO22) and print
 * every 7-bit address that ACKs. Useful for LCD1602 PCF8574AT (often 0x3F)
 * and keypad adapters (often 0x20–0x27).
 *
 *   ./scripts/fw idf upload i2c_scan
 *   ./scripts/fw idf monitor i2c_scan
 */

#include <stdio.h>

#include "driver/i2c_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "i2c_scan";

#define I2C_SDA_GPIO 21
#define I2C_SCL_GPIO 22
#define I2C_FREQ_HZ  100000 /* PCF8574 Standard-mode */

static void scan_bus(i2c_master_bus_handle_t bus)
{
    int found = 0;
    printf("\n");
    printf("I2C scan SDA=GPIO%d SCL=GPIO%d @ %d Hz\n", I2C_SDA_GPIO, I2C_SCL_GPIO, I2C_FREQ_HZ);
    printf("     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f\n");

    for (int row = 0; row < 8; row++) {
        printf("%02x:", row * 16);
        for (int col = 0; col < 16; col++) {
            int addr = row * 16 + col;
            if (addr < 0x08 || addr > 0x77) {
                printf("   ");
                continue;
            }
            esp_err_t err = i2c_master_probe(bus, (uint16_t)addr, 50 /* ms */);
            if (err == ESP_OK) {
                printf(" %02x", addr);
                found++;
            } else {
                printf(" --");
            }
        }
        printf("\n");
    }

    if (found == 0) {
        ESP_LOGW(TAG, "No devices responded. Check SDA/SCL wiring and pull-ups.");
    } else {
        ESP_LOGI(TAG, "Found %d device(s). LCD PCF8574AT often 0x3F; non-A keypad often 0x20-0x27.",
                 found);
    }
    printf("\n");
}

void app_main(void)
{
    ESP_LOGI(TAG, "i2c_scan starting");

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

    while (1) {
        scan_bus(bus);
        ESP_LOGI(TAG, "Rescan in 5 s…");
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
