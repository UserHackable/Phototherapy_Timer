/*
 * ESP-IDF app: tm1637_hello
 *
 * Bring-up for the 4-digit TM1637 clock module (CLK + DIO, not I²C).
 *
 * Wiring (project defaults — [docs/seven-segment-display.md]):
 *   CLK → GPIO18
 *   DIO → GPIO23
 *   VCC → 5 V
 *   GND → GND
 *
 * Demo:
 *   1. All segments / colon (88:88)
 *   2. Fixed 12:34 with colon
 *   3. Free-running MM:SS counter (colon blinks each second)
 *
 *   ./scripts/fw idf upload tm1637_hello
 *   ./scripts/fw idf monitor tm1637_hello
 */

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "tm1637.h"

static const char *TAG = "tm1637_hello";

/* Matches docs/esp32-board.md suggestion */
#define TM_CLK_GPIO 18
#define TM_DIO_GPIO 23

void app_main(void)
{
    ESP_LOGI(TAG, "tm1637_hello CLK=GPIO%d DIO=GPIO%d", TM_CLK_GPIO, TM_DIO_GPIO);

    tm1637_t disp;
    ESP_ERROR_CHECK(tm1637_init(&disp, TM_CLK_GPIO, TM_DIO_GPIO));
    tm1637_set_brightness(&disp, 5);

    ESP_LOGI(TAG, "pattern 88:88 (all segments + colon)");
    tm1637_show_pairs(&disp, 88, 88, true);
    vTaskDelay(pdMS_TO_TICKS(2000));

    ESP_LOGI(TAG, "pattern 12:34");
    tm1637_show_pairs(&disp, 12, 34, true);
    vTaskDelay(pdMS_TO_TICKS(2000));

    ESP_LOGI(TAG, "counting MM:SS with blinking colon");
    int secs = 0;
    while (1) {
        int mm = (secs / 60) % 100;
        int ss = secs % 60;
        bool colon = (secs % 2) == 0;
        tm1637_show_pairs(&disp, mm, ss, colon);
        ESP_LOGI(TAG, "display %02d:%02d colon=%d", mm, ss, colon);
        vTaskDelay(pdMS_TO_TICKS(1000));
        secs++;
        if (secs >= 3600) {
            secs = 0;
        }
    }
}
