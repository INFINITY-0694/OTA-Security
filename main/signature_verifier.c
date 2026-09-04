#include "signature_verifier.h"

#include "mbedtls/md.h"
#include "mbedtls/pk.h"

extern const uint8_t trusted_public_key_pem_start[]
    asm("_binary_trusted_public_key_pem_start");
extern const uint8_t trusted_public_key_pem_end[]
    asm("_binary_trusted_public_key_pem_end");

esp_err_t signature_verifier_verify(const uint8_t *data,
                                    size_t data_size,
                                    const uint8_t *signature,
                                    size_t signature_size)
{
    if (!data || !data_size || !signature || !signature_size) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t hash[32];
    const mbedtls_md_info_t *sha256 =
        mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (!sha256 || mbedtls_md(sha256, data, data_size, hash) != 0) {
        return ESP_FAIL;
    }

    mbedtls_pk_context public_key;
    mbedtls_pk_init(&public_key);

    size_t public_key_size = trusted_public_key_pem_end -
                             trusted_public_key_pem_start;
    int result = mbedtls_pk_parse_public_key(&public_key,
                                              trusted_public_key_pem_start,
                                              public_key_size);
    if (result == 0) {
        result = mbedtls_pk_verify(&public_key, MBEDTLS_MD_SHA256,
                                   hash, sizeof(hash), signature,
                                   signature_size);
    }

    mbedtls_pk_free(&public_key);
    return result == 0 ? ESP_OK : ESP_FAIL;
}
