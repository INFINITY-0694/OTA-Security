#include "crypto_manager.h"

#include "psa/crypto.h"

esp_err_t crypto_decrypt_firmware(const uint8_t *encrypted_firmware,
                                  size_t encrypted_size,
                                  const uint8_t *aes_key,
                                  const uint8_t *nonce,
                                  const uint8_t *tag,
                                  uint8_t *decrypted_firmware)
{
    if (!encrypted_firmware || !encrypted_size || !aes_key || !nonce || !tag ||
        !decrypted_firmware) {
        return ESP_ERR_INVALID_ARG;
    }

    if (psa_crypto_init() != PSA_SUCCESS) {
        return ESP_FAIL;
    }

    psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
    psa_key_id_t key_id = 0;
    psa_aead_operation_t operation = PSA_AEAD_OPERATION_INIT;
    size_t decrypted_size = 0;
    size_t output_size = 0;

    psa_set_key_type(&attributes, PSA_KEY_TYPE_AES);
    psa_set_key_bits(&attributes, OTA_AES_KEY_SIZE * 8);
    psa_set_key_usage_flags(&attributes, PSA_KEY_USAGE_DECRYPT);
    psa_set_key_algorithm(&attributes, PSA_ALG_GCM);

    psa_status_t status = psa_import_key(&attributes, aes_key,
                                          OTA_AES_KEY_SIZE, &key_id);
    psa_reset_key_attributes(&attributes);
    if (status == PSA_SUCCESS) {
        status = psa_aead_decrypt_setup(&operation, key_id, PSA_ALG_GCM);
    }
    if (status == PSA_SUCCESS) {
        status = psa_aead_set_nonce(&operation, nonce, OTA_AES_GCM_NONCE_SIZE);
    }
    if (status == PSA_SUCCESS) {
        status = psa_aead_update(&operation, encrypted_firmware, encrypted_size,
                                 decrypted_firmware, encrypted_size, &output_size);
        decrypted_size = output_size;
    }
    if (status == PSA_SUCCESS) {
        status = psa_aead_verify(&operation,
                                 decrypted_firmware + decrypted_size,
                                 encrypted_size - decrypted_size,
                                 &output_size, tag, OTA_AES_GCM_TAG_SIZE);
        decrypted_size += output_size;
    }

    psa_aead_abort(&operation);
    if (key_id != 0) {
        psa_destroy_key(key_id);
    }

    return status == PSA_SUCCESS && decrypted_size == encrypted_size ? ESP_OK : ESP_FAIL;
}

esp_err_t crypto_calculate_sha256(const uint8_t *data,
                                  size_t data_size,
                                  uint8_t hash[OTA_SHA256_SIZE])
{
    if (!data || !data_size || !hash) {
        return ESP_ERR_INVALID_ARG;
    }

    if (psa_crypto_init() != PSA_SUCCESS) {
        return ESP_FAIL;
    }

    size_t hash_size = 0;
    psa_status_t status = psa_hash_compute(PSA_ALG_SHA_256, data, data_size,
                                           hash, OTA_SHA256_SIZE, &hash_size);
    return status == PSA_SUCCESS && hash_size == OTA_SHA256_SIZE ? ESP_OK : ESP_FAIL;
}
