#pragma once

#include "driver/i2c_master.h"
#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    i2c_master_dev_handle_t dev;
    uint8_t bl_mask; /* PIN_BL when on, 0 when off */
} lcd1602_t;

/** Find backpack (prefer 0x3F A-variant) and init 16x2 in 4-bit mode. */
esp_err_t lcd1602_init(i2c_master_bus_handle_t bus, lcd1602_t *lcd, uint8_t *addr_out);

void lcd1602_clear(lcd1602_t *lcd);
void lcd1602_set_cursor(lcd1602_t *lcd, uint8_t col, uint8_t row);
void lcd1602_print(lcd1602_t *lcd, const char *s);
/** Write one full row (16 cols), space-padded — clears leftover garbage. */
void lcd1602_print_line(lcd1602_t *lcd, uint8_t row, const char *s);
void lcd1602_backlight(lcd1602_t *lcd, bool on);

#ifdef __cplusplus
}
#endif
