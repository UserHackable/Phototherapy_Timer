#pragma once

#include "driver/i2c_master.h"
#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 4×4 membrane via PCF8574 backpack (this bench: 0x20). */
typedef struct {
    i2c_master_dev_handle_t dev;
    uint8_t addr;
} keypad_i2c_t;

/**
 * Open expander at addr (probe first).
 * Matrix: P0–P3 rows (driven), P4–P7 columns (read, pulled high).
 */
esp_err_t keypad_i2c_init(i2c_master_bus_handle_t bus, keypad_i2c_t *kp, uint8_t addr);

/**
 * One scan. Returns true if a key is currently held.
 * *out is the label ('0'–'9', 'A'–'D', '*', '#') when true.
 */
bool keypad_i2c_scan(keypad_i2c_t *kp, char *out);

#ifdef __cplusplus
}
#endif
