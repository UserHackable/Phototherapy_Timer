/*
 * ESP-IDF app: lcd_hello
 *
 * Stable text on LCD1602 — write once, backlight stays on, no blink.
 *
 * Wiring: SDA=GPIO21, SCL=GPIO22, VCC=**5V**, GND=GND
 * If the blue glow is on but no letters: turn the contrast pot on the backpack.
 */

#include "driver/i2c_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lcd1602_pcf8574.h"

static const char *TAG = "lcd_hello";

#define I2C_SDA_GPIO 21
#define I2C_SCL_GPIO 22

void app_main(void)
{
    ESP_LOGI(TAG, "lcd_hello standard map, backlight forced  SDA=%d SCL=%d",
             I2C_SDA_GPIO, I2C_SCL_GPIO);

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
    uint8_t addr = 0;
    if (lcd1602_init(bus, &lcd, &addr) != ESP_OK) {
        ESP_LOGE(TAG, "LCD init failed");
        return;
    }

    lcd1602_backlight(&lcd, true);
    lcd1602_clear(&lcd);

    lcd1602_print_line(&lcd, 0, "Hello, world!");
    lcd1602_print_line(&lcd, 1, "ESP32 LCD1602");

    /* Re-assert backlight once more; never toggle it again */
    lcd1602_backlight(&lcd, true);

    ESP_LOGI(TAG, "Done addr=0x%02x. Expect blue backlight + text (or turn contrast pot).",
             addr);

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(60000));
    }
}
