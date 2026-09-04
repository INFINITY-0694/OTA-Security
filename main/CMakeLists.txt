idf_component_register(
    SRCS "main.c" "wifi_manager.c" "ota_manager.c" "crypto_manager.c"
         "signature_verifier.c" "manifest.c"
    INCLUDE_DIRS "."
    REQUIRES esp_wifi esp_event esp_netif nvs_flash esp_http_client esp_app_format app_update mbedtls
    EMBED_TXTFILES "trusted_public_key.pem"
)
