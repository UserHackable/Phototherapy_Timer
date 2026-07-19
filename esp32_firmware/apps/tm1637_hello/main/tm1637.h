#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int clk_gpio;
    int dio_gpio;
    uint8_t brightness; /* 0–7 */
} tm1637_t;

/** Configure GPIOs and blank the display (brightness still applied). */
esp_err_t tm1637_init(tm1637_t *dev, int clk_gpio, int dio_gpio);

/** Brightness 0 (dim) … 7 (bright). Display stays on. */
void tm1637_set_brightness(tm1637_t *dev, uint8_t level);

/** Turn display power on/off (segments cleared when off). */
void tm1637_display_on(tm1637_t *dev, bool on);

/** Write four raw segment bytes (bit0=A … bit6=G, bit7=DP/colon on digit1). */
void tm1637_set_segments(tm1637_t *dev, const uint8_t segs[4]);

/**
 * Show 0–9999 as four digits. If leading_zeros is false, blank leading zeros
 * (except the units place).
 */
void tm1637_show_number(tm1637_t *dev, int value, bool leading_zeros);

/**
 * Clock / countdown style: left pair and right pair (each 0–99), optional colon.
 * Examples: hours/minutes, minutes/seconds.
 */
void tm1637_show_pairs(tm1637_t *dev, int left, int right, bool colon);

/** Clear all digits. */
void tm1637_clear(tm1637_t *dev);

#ifdef __cplusplus
}
#endif
