#pragma once

#include <stddef.h>
#include <stdint.h>

#include "crypto_manager.h"
#include "esp_err.h"

#define OTA_MANIFEST_VERSION_LENGTH 16

typedef struct {
    char firmware_version[OTA_MANIFEST_VERSION_LENGTH + 1];
    uint32_t firmware_size;
    uint8_t nonce[OTA_AES_GCM_NONCE_SIZE];
    uint8_t tag[OTA_AES_GCM_TAG_SIZE];
    uint8_t firmware_hash[OTA_SHA256_SIZE];
    const uint8_t *signature;
    size_t signature_size;
    const uint8_t *encrypted_firmware;
    size_t encrypted_size;
    const uint8_t *signed_data;
    size_t signed_data_size;
} ota_manifest_t;

esp_err_t manifest_parse(const uint8_t *package_data,
                         size_t package_size,
                         ota_manifest_t *manifest);
