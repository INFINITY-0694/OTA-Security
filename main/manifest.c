#include "manifest.h"

#include <string.h>

#define OTA_PACKAGE_MAGIC "SOTA"
#define OTA_PACKAGE_FORMAT_VERSION 1
#define OTA_SIGNATURE_MAX_SIZE 80
#define OTA_FIXED_HEADER_SIZE 70

static uint32_t read_uint32(const uint8_t *data)
{
    return ((uint32_t)data[0] << 24) |
           ((uint32_t)data[1] << 16) |
           ((uint32_t)data[2] << 8) |
           data[3];
}

static uint16_t read_uint16(const uint8_t *data)
{
    return ((uint16_t)data[0] << 8) | data[1];
}

esp_err_t manifest_parse(const uint8_t *package_data,
                         size_t package_size,
                         ota_manifest_t *manifest)
{
    if (!package_data || !manifest || package_size < OTA_FIXED_HEADER_SIZE) {
        return ESP_ERR_INVALID_ARG;
    }
    if (memcmp(package_data, OTA_PACKAGE_MAGIC, 4) != 0 ||
        package_data[4] != OTA_PACKAGE_FORMAT_VERSION) {
        return ESP_FAIL;
    }

    size_t version_size = package_data[5];
    if (!version_size || version_size > OTA_MANIFEST_VERSION_LENGTH) {
        return ESP_FAIL;
    }

    size_t signed_data_size = OTA_FIXED_HEADER_SIZE + version_size;
    if (package_size < signed_data_size + 2) {
        return ESP_FAIL;
    }

    uint32_t firmware_size = read_uint32(package_data + 6);
    size_t signature_size = read_uint16(package_data + signed_data_size);
    size_t encrypted_offset = signed_data_size + 2 + signature_size;
    if (!firmware_size || !signature_size ||
        signature_size > OTA_SIGNATURE_MAX_SIZE ||
        encrypted_offset > package_size ||
        package_size - encrypted_offset != firmware_size) {
        return ESP_FAIL;
    }

    memset(manifest, 0, sizeof(*manifest));
    memcpy(manifest->firmware_version, package_data + OTA_FIXED_HEADER_SIZE,
           version_size);
    manifest->firmware_size = firmware_size;
    memcpy(manifest->nonce, package_data + 10, OTA_AES_GCM_NONCE_SIZE);
    memcpy(manifest->tag, package_data + 22, OTA_AES_GCM_TAG_SIZE);
    memcpy(manifest->firmware_hash, package_data + 38, OTA_SHA256_SIZE);
    manifest->signature = package_data + signed_data_size + 2;
    manifest->signature_size = signature_size;
    manifest->encrypted_firmware = package_data + encrypted_offset;
    manifest->encrypted_size = firmware_size;
    manifest->signed_data = package_data;
    manifest->signed_data_size = signed_data_size;
    return ESP_OK;
}
