/*
 * 4×4 matrix keypad over PCF8574 (I²C).
 *
 * Port map (this bench, verified row-by-row):
 *   Drive P0..P3 low one at a time; read P4..P7 (0 = pressed).
 *   Physical silk (row-major) maps to expander indices as:
 *     label = KEYMAP[3 - col][3 - row]
 *   (matrix is transposed and both axes reversed vs a naive scan.)
 *
 * Key legend (membrane silk):
 *   1 2 3 A
 *   4 5 6 B
 *   7 8 9 C
 *   * 0 # D
 */

#include "keypad_i2c.h"

#include "esp_log.h"
#include "esp_rom_sys.h"

#include <string.h>

static const char *TAG = "keypad_i2c";

/* Physical labels [membrane_row][membrane_col] */
static const char KEYMAP[4][4] = {
    {'1', '2', '3', 'A'},
    {'4', '5', '6', 'B'},
    {'7', '8', '9', 'C'},
    {'*', '0', '#', 'D'},
};

/* Short timeout: Wi‑Fi can make I²C flaky; don't stall the UI for 100ms×N */
#define KP_I2C_MS 30

static esp_err_t pcf_write(keypad_i2c_t *kp, uint8_t v)
{
    esp_err_t err = i2c_master_transmit(kp->dev, &v, 1, KP_I2C_MS);
    if (err != ESP_OK) {
        /* one quick retry */
        err = i2c_master_transmit(kp->dev, &v, 1, KP_I2C_MS);
    }
    return err;
}

static esp_err_t pcf_read(keypad_i2c_t *kp, uint8_t *v)
{
    esp_err_t err = i2c_master_receive(kp->dev, v, 1, KP_I2C_MS);
    if (err != ESP_OK) {
        err = i2c_master_receive(kp->dev, v, 1, KP_I2C_MS);
    }
    return err;
}

esp_err_t keypad_i2c_init(i2c_master_bus_handle_t bus, keypad_i2c_t *kp, uint8_t addr)
{
    if (!kp) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(kp, 0, sizeof(*kp));

    if (i2c_master_probe(bus, addr, 100) != ESP_OK) {
        ESP_LOGE(TAG, "no device at 0x%02x", addr);
        return ESP_ERR_NOT_FOUND;
    }

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = addr,
        .scl_speed_hz = 50000, /* same order as LCD; keep modest with Wi‑Fi */
    };
    i2c_master_dev_handle_t dev = NULL;
    esp_err_t err = i2c_master_bus_add_device(bus, &dev_cfg, &dev);
    if (err != ESP_OK) {
        return err;
    }

    kp->dev = dev;
    kp->addr = addr;

    /* All high: rows idle high, columns pulled up for reads */
    err = pcf_write(kp, 0xFF);
    if (err != ESP_OK) {
        return err;
    }

    ESP_LOGI(TAG, "keypad ready at 0x%02x (rows P0-3, cols P4-7)", addr);
    return ESP_OK;
}

bool keypad_i2c_scan(keypad_i2c_t *kp, char *out)
{
    if (!kp || !out) {
        return false;
    }
    *out = '\0';

    for (int row = 0; row < 4; row++) {
        /* Drive this row low; other rows high; columns high (inputs) */
        uint8_t drive = (uint8_t)(0xFF & ~(1u << row));
        if (pcf_write(kp, drive) != ESP_OK) {
            continue;
        }
        esp_rom_delay_us(150); /* settle after row drive (helps with longer bus + Wi‑Fi) */

        uint8_t sample = 0xFF;
        if (pcf_read(kp, &sample) != ESP_OK) {
            continue;
        }

        for (int col = 0; col < 4; col++) {
            /* Column pressed if bit (4+col) is low */
            if ((sample & (1u << (4 + col))) == 0) {
                /* Verified: silk[r][c] ↔ drive row (3-c), col bit (3-r) */
                *out = KEYMAP[3 - col][3 - row];
                /* Release rows high before return */
                pcf_write(kp, 0xFF);
                return true;
            }
        }
    }

    pcf_write(kp, 0xFF);
    return false;
}
