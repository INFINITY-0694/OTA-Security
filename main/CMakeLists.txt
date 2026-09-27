idf_component_register(
    SRCS "ota_manager.c" "main.c" "wifi_manager.c"
    INCLUDE_DIRS "."
    EMBED_TXTFILES "ota_public_key.pem"
    REQUIRES esp_wifi esp_event esp_netif nvs_flash esp_http_client esp_app_format app_update mbedtls cjson
)
