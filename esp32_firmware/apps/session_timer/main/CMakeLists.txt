idf_component_register(
  SRCS "main.c" "lcd1602_pcf8574.c" "tm1637.c" "keypad_i2c.c"
  INCLUDE_DIRS "."
  # Do not set REQUIRES alone — that drops main's default common deps.
  # json = cJSON for UDP discovery payloads.
  PRIV_REQUIRES json driver esp_wifi nvs_flash esp_event esp_netif esp_timer esp_driver_gpio esp_driver_i2c esp_driver_ledc
)
