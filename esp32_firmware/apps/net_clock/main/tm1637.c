/*
 * Minimal TM1637 bit-bang driver (not I²C).
 *
 * Protocol matches common Arduino TM1637Display libraries:
 *   start → cmd → stop; start → addr + 4 data → stop; start → display ctrl → stop
 *
 * Segment map (bit): 0=A 1=B 2=C 3=D 4=E 5=F 6=G 7=DP
 * On clock modules the colon is usually bit7 of the second digit (index 1).
 */

#include "tm1637.h"

#include "driver/gpio.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <string.h>

/* ~bit half-period; TM1637 is happy well below 100 kHz */
#define TM_DELAY_US 5

/* Digit → segments (no DP) */
static const uint8_t DIGIT_SEG[] = {
    0x3f, /* 0 */
    0x06, /* 1 */
    0x5b, /* 2 */
    0x4f, /* 3 */
    0x66, /* 4 */
    0x6d, /* 5 */
    0x7d, /* 6 */
    0x07, /* 7 */
    0x7f, /* 8 */
    0x6f, /* 9 */
};

#define SEG_BLANK 0x00
#define COLON_BIT 0x80

static void dio_output(tm1637_t *dev)
{
    gpio_set_direction(dev->dio_gpio, GPIO_MODE_OUTPUT);
}

static void dio_input(tm1637_t *dev)
{
    gpio_set_direction(dev->dio_gpio, GPIO_MODE_INPUT);
}

static void clk_high(tm1637_t *dev)
{
    gpio_set_level(dev->clk_gpio, 1);
}

static void clk_low(tm1637_t *dev)
{
    gpio_set_level(dev->clk_gpio, 0);
}

static void dio_high(tm1637_t *dev)
{
    gpio_set_level(dev->dio_gpio, 1);
}

static void dio_low(tm1637_t *dev)
{
    gpio_set_level(dev->dio_gpio, 0);
}

static void delay_half(void)
{
    esp_rom_delay_us(TM_DELAY_US);
}

static void tm_start(tm1637_t *dev)
{
    dio_output(dev);
    dio_high(dev);
    clk_high(dev);
    delay_half();
    dio_low(dev);
    delay_half();
    clk_low(dev);
    delay_half();
}

static void tm_stop(tm1637_t *dev)
{
    dio_output(dev);
    clk_low(dev);
    dio_low(dev);
    delay_half();
    clk_high(dev);
    delay_half();
    dio_high(dev);
    delay_half();
}

/** Write one byte LSB-first; ignore ACK. */
static void tm_write_byte(tm1637_t *dev, uint8_t b)
{
    dio_output(dev);
    for (int i = 0; i < 8; i++) {
        clk_low(dev);
        if (b & 0x01) {
            dio_high(dev);
        } else {
            dio_low(dev);
        }
        delay_half();
        clk_high(dev);
        delay_half();
        b >>= 1;
    }
    /* ACK clock: release DIO, pulse CLK */
    clk_low(dev);
    dio_input(dev);
    delay_half();
    clk_high(dev);
    delay_half();
    clk_low(dev);
    dio_output(dev);
    delay_half();
}

static void tm_write_cmd(tm1637_t *dev, uint8_t cmd)
{
    tm_start(dev);
    tm_write_byte(dev, cmd);
    tm_stop(dev);
}

esp_err_t tm1637_init(tm1637_t *dev, int clk_gpio, int dio_gpio)
{
    if (!dev) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(dev, 0, sizeof(*dev));
    dev->clk_gpio = clk_gpio;
    dev->dio_gpio = dio_gpio;
    dev->brightness = 5;

    gpio_config_t io = {
        .pin_bit_mask = (1ULL << clk_gpio) | (1ULL << dio_gpio),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&io);
    if (err != ESP_OK) {
        return err;
    }

    clk_high(dev);
    dio_high(dev);
    tm1637_clear(dev);
    tm1637_display_on(dev, true);
    return ESP_OK;
}

void tm1637_set_brightness(tm1637_t *dev, uint8_t level)
{
    if (level > 7) {
        level = 7;
    }
    dev->brightness = level;
    /* 0x88 | level: display on + brightness */
    tm_write_cmd(dev, (uint8_t)(0x88 | level));
}

void tm1637_display_on(tm1637_t *dev, bool on)
{
    if (on) {
        tm_write_cmd(dev, (uint8_t)(0x88 | (dev->brightness & 7)));
    } else {
        tm_write_cmd(dev, 0x80); /* display off */
    }
}

void tm1637_set_segments(tm1637_t *dev, const uint8_t segs[4])
{
    /* Auto-increment write from address 0 */
    tm_write_cmd(dev, 0x40);
    tm_start(dev);
    tm_write_byte(dev, 0xC0); /* address 0 */
    for (int i = 0; i < 4; i++) {
        tm_write_byte(dev, segs[i]);
    }
    tm_stop(dev);
    tm1637_set_brightness(dev, dev->brightness);
}

void tm1637_clear(tm1637_t *dev)
{
    uint8_t blank[4] = {SEG_BLANK, SEG_BLANK, SEG_BLANK, SEG_BLANK};
    tm1637_set_segments(dev, blank);
}

void tm1637_show_number(tm1637_t *dev, int value, bool leading_zeros)
{
    if (value < 0) {
        value = 0;
    }
    if (value > 9999) {
        value = 9999;
    }

    int d[4];
    d[0] = (value / 1000) % 10;
    d[1] = (value / 100) % 10;
    d[2] = (value / 10) % 10;
    d[3] = value % 10;

    uint8_t segs[4];
    bool started = leading_zeros;
    for (int i = 0; i < 4; i++) {
        if (!started && d[i] == 0 && i < 3) {
            segs[i] = SEG_BLANK;
        } else {
            started = true;
            segs[i] = DIGIT_SEG[d[i]];
        }
    }
    tm1637_set_segments(dev, segs);
}

void tm1637_show_pairs(tm1637_t *dev, int left, int right, bool colon)
{
    if (left < 0) {
        left = 0;
    }
    if (left > 99) {
        left = 99;
    }
    if (right < 0) {
        right = 0;
    }
    if (right > 99) {
        right = 99;
    }

    uint8_t segs[4];
    segs[0] = DIGIT_SEG[(left / 10) % 10];
    segs[1] = DIGIT_SEG[left % 10];
    segs[2] = DIGIT_SEG[(right / 10) % 10];
    segs[3] = DIGIT_SEG[right % 10];
    if (colon) {
        segs[1] |= COLON_BIT;
    }
    tm1637_set_segments(dev, segs);
}
