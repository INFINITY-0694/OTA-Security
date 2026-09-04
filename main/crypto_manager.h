#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#define OTA_AES_KEY_SIZE 32
#define OTA_AES_GCM_NONCE_SIZE 12
#define OTA_AES_GCM_TAG_SIZE 16
#define OTA_SHA256_SIZE 32

esp_err_t crypto_decrypt_firmware(const uint8_t *encrypted_firmware,
                                  size_t encrypted_size,
                                  const uint8_t *aes_key,
                                  const uint8_t *nonce,
                                  const uint8_t *tag,
                                  uint8_t *decrypted_firmware);

esp_err_t crypto_calculate_sha256(const uint8_t *data,
                                  size_t data_size,
                                  uint8_t hash[OTA_SHA256_SIZE]);
