idf_component_register(
  SRCS "main.c"
  INCLUDE_DIRS "."
  PRIV_REQUIRES json nvs_flash esp_event esp_netif esp_wifi esp_timer
)
