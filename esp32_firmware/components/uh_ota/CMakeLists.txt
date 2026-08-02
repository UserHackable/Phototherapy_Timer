idf_component_register(
    SRCS "uh_ota.c"
    INCLUDE_DIRS "include"
    REQUIRES
        json
        esp_http_client
        app_update
        esp_partition
        mbedtls
        esp_app_format
        log
        freertos
)
