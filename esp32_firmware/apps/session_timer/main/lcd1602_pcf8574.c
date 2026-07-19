/*
 * HD44780 via PCF8574 backpack — LiquidCrystal_I2C / HW-061 pin map:
 *   P0=RS  P1=RW  P2=E  P3=Backlight  P4=D4  P5=D5  P6=D6  P7=D7
 *
 * Address on this board: 0x27 (confirmed by i2c_scan). Chip silk may say
 * PCF8574AT but the live 7-bit address is the non-A range.
 *
 * Backlight is P3 active-high — always OR'd into every port write so the
 * panel does not go dark between nibbles.
 */

#include "lcd1602_pcf8574.h"

#include "esp_log.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <string.h>

static const char *TAG = "lcd1602";

#define PIN_RS (1u << 0)
#define PIN_RW (1u << 1)
#define PIN_EN (1u << 2)
#define PIN_BL (1u << 3)

static esp_err_t exp_write(lcd1602_t *lcd, uint8_t data)
{
    /* Keep backlight latched on every transaction */
    uint8_t out = (uint8_t)(data | lcd->bl_mask);
    /* 50 ms max — avoid multi-second stalls if the bus glitches under Wi‑Fi */
    return i2c_master_transmit(lcd->dev, &out, 1, 50);
}

static void pulse_enable(lcd1602_t *lcd, uint8_t data)
{
    exp_write(lcd, (uint8_t)(data | PIN_EN));
    esp_rom_delay_us(10);
    exp_write(lcd, (uint8_t)(data & ~PIN_EN));
    esp_rom_delay_us(100);
}

/** value already has high nibble in bits 7..4 and mode bits in low nibble */
static void write4bits(lcd1602_t *lcd, uint8_t value)
{
    exp_write(lcd, value);
    pulse_enable(lcd, value);
}

static void send(lcd1602_t *lcd, uint8_t value, uint8_t mode)
{
    uint8_t high = (uint8_t)((value & 0xF0) | mode);
    uint8_t low = (uint8_t)(((value << 4) & 0xF0) | mode);
    write4bits(lcd, high);
    write4bits(lcd, low);
}

static void command(lcd1602_t *lcd, uint8_t cmd)
{
    send(lcd, cmd, 0);
    if (cmd == 0x01 || cmd == 0x02) {
        vTaskDelay(pdMS_TO_TICKS(5));
    } else {
        esp_rom_delay_us(100);
    }
}

static void write_char(lcd1602_t *lcd, uint8_t c)
{
    send(lcd, c, PIN_RS);
    esp_rom_delay_us(100);
}

static esp_err_t begin_4bit(lcd1602_t *lcd)
{
    /* Power-up settle */
    vTaskDelay(pdMS_TO_TICKS(100));

    /* Backlight on, bus idle (RS/RW/E low, data 0) */
    lcd->bl_mask = PIN_BL;
    exp_write(lcd, 0);
    vTaskDelay(pdMS_TO_TICKS(50));

    /*
     * HD44780 4-bit init (datasheet fig. 24 / LiquidCrystal_I2C::begin).
     * Only the high nibble is significant during this phase.
     */
    write4bits(lcd, 0x03 << 4);
    vTaskDelay(pdMS_TO_TICKS(5));
    write4bits(lcd, 0x03 << 4);
    vTaskDelay(pdMS_TO_TICKS(5));
    write4bits(lcd, 0x03 << 4);
    esp_rom_delay_us(200);
    write4bits(lcd, 0x02 << 4); /* switch to 4-bit */
    esp_rom_delay_us(200);

    /* Function set — send twice; some clones need the second */
    command(lcd, 0x28); /* 4-bit, 2 lines, 5x8 */
    command(lcd, 0x28);
    command(lcd, 0x08); /* display off */
    command(lcd, 0x01); /* clear */
    command(lcd, 0x06); /* entry: inc, no shift */
    command(lcd, 0x0C); /* display on, cursor off, blink off */
    return ESP_OK;
}

esp_err_t lcd1602_init(i2c_master_bus_handle_t bus, lcd1602_t *lcd, uint8_t *addr_out)
{
    /* Prefer the address we actually scanned (0x27), then common fallbacks */
    static const uint8_t candidates[] = {
        0x27, 0x3F, 0x26, 0x3E, 0x20, 0x38,
    };

    for (size_t i = 0; i < sizeof(candidates); i++) {
        uint8_t addr = candidates[i];
        if (i2c_master_probe(bus, addr, 100) != ESP_OK) {
            continue;
        }

        i2c_device_config_t dev_cfg = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = addr,
            /* Slow bus: 3.3 V ESP32 driving 5 V PCF8574 is marginal on VIH */
            .scl_speed_hz = 20000,
        };
        i2c_master_dev_handle_t dev = NULL;
        if (i2c_master_bus_add_device(bus, &dev_cfg, &dev) != ESP_OK) {
            continue;
        }

        memset(lcd, 0, sizeof(*lcd));
        lcd->dev = dev;
        lcd->bl_mask = PIN_BL;

        /* Light the backlight immediately so a dark panel is not "dead" */
        exp_write(lcd, 0);
        ESP_LOGI(TAG, "Backlight on (P3), addr=0x%02x — adjust contrast pot if blank", addr);

        if (begin_4bit(lcd) != ESP_OK) {
            i2c_master_bus_rm_device(dev);
            continue;
        }

        if (addr_out) {
            *addr_out = addr;
        }
        ESP_LOGI(TAG, "LCD ready at 0x%02x (standard P0=RS map)", addr);
        return ESP_OK;
    }

    ESP_LOGE(TAG, "No LCD backpack found on I2C");
    return ESP_ERR_NOT_FOUND;
}

void lcd1602_clear(lcd1602_t *lcd)
{
    command(lcd, 0x01);
}

void lcd1602_set_cursor(lcd1602_t *lcd, uint8_t col, uint8_t row)
{
    const uint8_t row_offsets[] = {0x00, 0x40};
    if (row > 1) {
        row = 1;
    }
    if (col > 15) {
        col = 15;
    }
    command(lcd, (uint8_t)(0x80 | (col + row_offsets[row])));
}

void lcd1602_print(lcd1602_t *lcd, const char *s)
{
    if (!s) {
        return;
    }
    while (*s) {
        write_char(lcd, (uint8_t)*s++);
    }
}

void lcd1602_print_line(lcd1602_t *lcd, uint8_t row, const char *s)
{
    char buf[16];
    size_t i = 0;
    if (s) {
        while (i < 16 && s[i]) {
            buf[i] = s[i];
            i++;
        }
    }
    while (i < 16) {
        buf[i++] = ' ';
    }
    lcd1602_set_cursor(lcd, 0, row);
    for (i = 0; i < 16; i++) {
        write_char(lcd, (uint8_t)buf[i]);
    }
}

void lcd1602_backlight(lcd1602_t *lcd, bool on)
{
    lcd->bl_mask = on ? PIN_BL : 0;
    exp_write(lcd, 0);
}
