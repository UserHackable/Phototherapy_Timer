/*
 * ESP-IDF app: blink
 *
 * Onboard LED GPIO 2, 250 ms half-period (2× Arduino blink rate).
 * SSR / lamp path not used.
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "blink";

#define LED_GPIO 2
#define BLINK_HALF_MS 250

void app_main(void)
{
    ESP_LOGI(TAG, "blink: GPIO %d, %d ms half-period", LED_GPIO, BLINK_HALF_MS);

    gpio_config_t io = {
        .pin_bit_mask = 1ULL << LED_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);

    int level = 0;
    while (1) {
        level = !level;
        gpio_set_level(LED_GPIO, level);
        vTaskDelay(pdMS_TO_TICKS(BLINK_HALF_MS));
    }
}
