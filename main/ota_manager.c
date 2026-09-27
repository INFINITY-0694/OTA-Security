#include "ota_manager.h"

#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "nvs.h"
#include "mbedtls/base64.h"
#include "mbedtls/pk.h"
#include "psa/crypto.h"

#define OTA_CHECK_INTERVAL_SECONDS 30
#define OTA_HTTP_BUFFER_SIZE 128
#define OTA_MANIFEST_MAX_SIZE 24576
#define OTA_DOWNLOAD_BUFFER_SIZE 4096
#define OTA_CHUNK_SIZE (32 * 1024)
#define OTA_MAX_CHUNKS 64
#define OTA_SHA256_HEX_SIZE 64
#define OTA_AES_KEY_SIZE 32
#define OTA_GCM_NONCE_SIZE 12
#define OTA_GCM_TAG_SIZE 16
#define OTA_SIGNATURE_SIZE 512

extern const uint8_t _binary_ota_public_key_pem_start[];
extern const uint8_t _binary_ota_public_key_pem_end[];

static const char *TAG = "OTA";
static char current_version[OTA_HTTP_BUFFER_SIZE];
static uint8_t device_aes_key[OTA_AES_KEY_SIZE];

typedef struct {
    size_t offset;
    size_t size;
    size_t encrypted_size;
    uint8_t sha256[32];
    char sha256_hex[OTA_SHA256_HEX_SIZE + 1];
    uint8_t nonce[OTA_GCM_NONCE_SIZE];
    uint8_t tag[OTA_GCM_TAG_SIZE];
    char nonce_b64[24];
    char tag_b64[25];
    uint8_t signature[OTA_SIGNATURE_SIZE];
    size_t signature_len;
} ota_chunk_t;

typedef struct {
    char version[OTA_HTTP_BUFFER_SIZE];
    size_t size;
    size_t encrypted_size;
    uint8_t sha256[32];
    char sha256_hex[OTA_SHA256_HEX_SIZE + 1];
    ota_chunk_t *chunks;
    size_t chunk_count;
} ota_manifest_t;

static void close_http_client(esp_http_client_handle_t client)
{
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
}

static bool parse_version(const char *version, int *major, int *minor, int *patch)
{
    char extra;
    int values_read = sscanf(version, "%d.%d.%d%c", major, minor, patch, &extra);
    return values_read == 3 && *major >= 0 && *minor >= 0 && *patch >= 0;
}

static bool version_is_newer(const char *candidate, const char *running)
{
    int candidate_major;
    int candidate_minor;
    int candidate_patch;
    int running_major;
    int running_minor;
    int running_patch;

    if (!parse_version(candidate, &candidate_major, &candidate_minor, &candidate_patch) ||
        !parse_version(running, &running_major, &running_minor, &running_patch)) {
        return false;
    }
    if (candidate_major != running_major) return candidate_major > running_major;
    if (candidate_minor != running_minor) return candidate_minor > running_minor;
    return candidate_patch > running_patch;
}

static bool hex_to_bytes(const char *hex, uint8_t *bytes, size_t byte_count)
{
    if (strlen(hex) != byte_count * 2) return false;
    for (size_t index = 0; index < byte_count; ++index) {
        if (!isxdigit((unsigned char)hex[index * 2]) ||
            !isxdigit((unsigned char)hex[index * 2 + 1])) return false;
        unsigned int value;
        if (sscanf(hex + index * 2, "%2x", &value) != 1) return false;
        bytes[index] = (uint8_t)value;
    }
    return true;
}

static esp_err_t load_device_aes_key(void)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open("ota_keys", NVS_READWRITE, &nvs);
    if (err != ESP_OK) return err;

    size_t length = sizeof(device_aes_key);
    err = nvs_get_blob(nvs, "aes256", device_aes_key, &length);
    if (err == ESP_OK && length == sizeof(device_aes_key)) {
        nvs_close(nvs);
        return ESP_OK;
    }

#ifdef CONFIG_OTA_FACTORY_AES_KEY_HEX
    if (strlen(CONFIG_OTA_FACTORY_AES_KEY_HEX) == OTA_AES_KEY_SIZE * 2 &&
        hex_to_bytes(CONFIG_OTA_FACTORY_AES_KEY_HEX, device_aes_key,
                     sizeof(device_aes_key))) {
        err = nvs_set_blob(nvs, "aes256", device_aes_key, sizeof(device_aes_key));
        if (err == ESP_OK) err = nvs_commit(nvs);
        nvs_close(nvs);
        return err;
    }
#endif
    nvs_close(nvs);
    return ESP_ERR_NOT_FOUND;
}

static esp_err_t read_http_body(const char *url, char *body, size_t body_size)
{
    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .timeout_ms = 10000,
        .buffer_size = 1024,
        .buffer_size_tx = 1024,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) return ESP_ERR_NO_MEM;

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        close_http_client(client);
        return err;
    }
    int content_length = esp_http_client_fetch_headers(client);
    if (esp_http_client_get_status_code(client) != 200 ||
        content_length > (int)body_size - 1) {
        close_http_client(client);
        return ESP_FAIL;
    }

    size_t total_read = 0;
    while (total_read < body_size - 1) {
        int bytes_read = esp_http_client_read(client, body + total_read,
                                               body_size - 1 - total_read);
        if (bytes_read < 0) {
            close_http_client(client);
            return ESP_FAIL;
        }
        if (bytes_read == 0) break;
        total_read += (size_t)bytes_read;
    }
    body[total_read] = '\0';
    bool complete = esp_http_client_is_complete_data_received(client);
    close_http_client(client);
    return complete ? ESP_OK : ESP_FAIL;
}

static bool json_size(const cJSON *item, size_t *value)
{
    if (!cJSON_IsNumber(item) || item->valuedouble < 0 ||
        item->valuedouble > (double)SIZE_MAX ||
        item->valuedouble != (double)(size_t)item->valuedouble) return false;
    *value = (size_t)item->valuedouble;
    return true;
}

static bool copy_json_string(const cJSON *item, char *output, size_t output_size)
{
    if (!cJSON_IsString(item) || !item->valuestring ||
        strlen(item->valuestring) >= output_size) return false;
    snprintf(output, output_size, "%s", item->valuestring);
    return true;
}

static void free_manifest(ota_manifest_t *manifest)
{
    free(manifest->chunks);
    memset(manifest, 0, sizeof(*manifest));
}

static esp_err_t parse_manifest(const char *body, ota_manifest_t *manifest)
{
    memset(manifest, 0, sizeof(*manifest));
    cJSON *root = cJSON_Parse(body);
    if (!root) return ESP_ERR_INVALID_RESPONSE;

    const cJSON *version = cJSON_GetObjectItemCaseSensitive(root, "version");
    const cJSON *size_item = cJSON_GetObjectItemCaseSensitive(root, "size");
    const cJSON *encrypted_size_item = cJSON_GetObjectItemCaseSensitive(root, "encrypted_size");
    const cJSON *sha256 = cJSON_GetObjectItemCaseSensitive(root, "sha256");
    const cJSON *chunk_size_item = cJSON_GetObjectItemCaseSensitive(root, "chunk_size");
    const cJSON *chunks = cJSON_GetObjectItemCaseSensitive(root, "chunks");
    size_t chunk_size;
    int chunk_count = cJSON_GetArraySize(chunks);
    bool valid = copy_json_string(version, manifest->version, sizeof(manifest->version)) &&
                 json_size(size_item, &manifest->size) && manifest->size > 0 &&
                 json_size(encrypted_size_item, &manifest->encrypted_size) &&
                 json_size(chunk_size_item, &chunk_size) && chunk_size == OTA_CHUNK_SIZE &&
                 copy_json_string(sha256, manifest->sha256_hex,
                                  sizeof(manifest->sha256_hex)) &&
                 strlen(manifest->sha256_hex) == OTA_SHA256_HEX_SIZE &&
                 cJSON_IsArray(chunks) && chunk_count > 0 && chunk_count <= OTA_MAX_CHUNKS;
    if (!valid || !hex_to_bytes(manifest->sha256_hex, manifest->sha256,
                                sizeof(manifest->sha256))) goto invalid;

    manifest->chunk_count = (size_t)chunk_count;
    manifest->chunks = calloc(manifest->chunk_count, sizeof(*manifest->chunks));
    if (!manifest->chunks) {
        cJSON_Delete(root);
        return ESP_ERR_NO_MEM;
    }

    size_t total_size = 0;
    size_t total_encrypted_size = 0;
    for (size_t index = 0; index < manifest->chunk_count; ++index) {
        const cJSON *item = cJSON_GetArrayItem(chunks, (int)index);
        const cJSON *chunk_index = cJSON_GetObjectItemCaseSensitive(item, "index");
        const cJSON *offset_item = cJSON_GetObjectItemCaseSensitive(item, "offset");
        const cJSON *chunk_size_value = cJSON_GetObjectItemCaseSensitive(item, "size");
        const cJSON *chunk_encrypted_size = cJSON_GetObjectItemCaseSensitive(item, "encrypted_size");
        const cJSON *chunk_sha256 = cJSON_GetObjectItemCaseSensitive(item, "sha256");
        const cJSON *nonce = cJSON_GetObjectItemCaseSensitive(item, "nonce");
        const cJSON *tag = cJSON_GetObjectItemCaseSensitive(item, "tag");
        const cJSON *signature = cJSON_GetObjectItemCaseSensitive(item, "signature");
        ota_chunk_t *chunk = &manifest->chunks[index];
        size_t parsed_index;
        size_t decoded_size = 0;
        valid = json_size(chunk_index, &parsed_index) && parsed_index == index &&
                json_size(offset_item, &chunk->offset) && chunk->offset == total_size &&
                json_size(chunk_size_value, &chunk->size) && chunk->size > 0 &&
                chunk->size <= OTA_CHUNK_SIZE &&
                (index + 1 == manifest->chunk_count || chunk->size == OTA_CHUNK_SIZE) &&
                json_size(chunk_encrypted_size, &chunk->encrypted_size) &&
                chunk->encrypted_size == chunk->size + OTA_GCM_TAG_SIZE &&
                copy_json_string(chunk_sha256, chunk->sha256_hex,
                                 sizeof(chunk->sha256_hex)) &&
                strlen(chunk->sha256_hex) == OTA_SHA256_HEX_SIZE &&
                copy_json_string(nonce, chunk->nonce_b64, sizeof(chunk->nonce_b64)) &&
                copy_json_string(tag, chunk->tag_b64, sizeof(chunk->tag_b64)) &&
                cJSON_IsString(signature) && signature->valuestring &&
                strlen(signature->valuestring) > 0 &&
                strlen(signature->valuestring) < OTA_SIGNATURE_SIZE * 2;
        if (!valid || !hex_to_bytes(chunk->sha256_hex, chunk->sha256,
                                    sizeof(chunk->sha256))) goto invalid;
        if (mbedtls_base64_decode(chunk->nonce, sizeof(chunk->nonce), &decoded_size,
                                  (const unsigned char *)chunk->nonce_b64,
                                  strlen(chunk->nonce_b64)) != 0 ||
            decoded_size != sizeof(chunk->nonce)) goto invalid;
        if (mbedtls_base64_decode(chunk->tag, sizeof(chunk->tag), &decoded_size,
                                  (const unsigned char *)chunk->tag_b64,
                                  strlen(chunk->tag_b64)) != 0 ||
            decoded_size != sizeof(chunk->tag)) goto invalid;
        if (mbedtls_base64_decode(chunk->signature, sizeof(chunk->signature),
                                  &chunk->signature_len,
                                  (const unsigned char *)signature->valuestring,
                                  strlen(signature->valuestring)) != 0 ||
            chunk->signature_len == 0) goto invalid;
        if (chunk->size > SIZE_MAX - total_size ||
            chunk->encrypted_size > SIZE_MAX - total_encrypted_size) goto invalid;
        total_size += chunk->size;
        total_encrypted_size += chunk->encrypted_size;
    }
    if (total_size != manifest->size || total_encrypted_size != manifest->encrypted_size) {
        goto invalid;
    }
    cJSON_Delete(root);
    return ESP_OK;

invalid:
    cJSON_Delete(root);
    free_manifest(manifest);
    return ESP_ERR_INVALID_RESPONSE;
}

static esp_err_t verify_manifest(const ota_manifest_t *manifest)
{
    mbedtls_pk_context public_key;
    mbedtls_pk_init(&public_key);
    size_t public_key_length = (size_t)(_binary_ota_public_key_pem_end -
                                        _binary_ota_public_key_pem_start);
    int result = mbedtls_pk_parse_public_key(&public_key, _binary_ota_public_key_pem_start,
                                             public_key_length);
    for (size_t index = 0; result == 0 && index < manifest->chunk_count; ++index) {
        const ota_chunk_t *chunk = &manifest->chunks[index];
        char payload[OTA_HTTP_BUFFER_SIZE + 192];
        int payload_length = snprintf(payload, sizeof(payload),
                                      "%s\n%zu\n%zu\n%zu\n%zu\n%s\n%s\n%s\n",
                                      manifest->version, index, chunk->offset,
                                      chunk->size, chunk->encrypted_size,
                                      chunk->sha256_hex, chunk->nonce_b64,
                                      chunk->tag_b64);
        if (payload_length < 0 || (size_t)payload_length >= sizeof(payload)) {
            result = -1;
            break;
        }
        uint8_t payload_digest[32];
        size_t payload_digest_length = 0;
        psa_hash_operation_t hash_operation = PSA_HASH_OPERATION_INIT;
        psa_status_t status = psa_hash_setup(&hash_operation, PSA_ALG_SHA_256);
        if (status == PSA_SUCCESS) {
            status = psa_hash_update(&hash_operation, (const uint8_t *)payload,
                                     (size_t)payload_length);
        }
        if (status == PSA_SUCCESS) {
            status = psa_hash_finish(&hash_operation, payload_digest,
                                     sizeof(payload_digest), &payload_digest_length);
        } else {
            psa_hash_abort(&hash_operation);
        }
        if (status != PSA_SUCCESS || payload_digest_length != sizeof(payload_digest)) {
            result = -1;
            break;
        }
        result = mbedtls_pk_verify(&public_key, MBEDTLS_MD_SHA256, payload_digest,
                                   sizeof(payload_digest), chunk->signature,
                                   chunk->signature_len);
        if (result == 0 && index + 1 < manifest->chunk_count) vTaskDelay(1);
    }
    mbedtls_pk_free(&public_key);
    return result == 0 ? ESP_OK : ESP_FAIL;
}

static esp_err_t fetch_and_verify_manifest(ota_manifest_t *manifest)
{
    char *body = malloc(OTA_MANIFEST_MAX_SIZE);
    if (!body) return ESP_ERR_NO_MEM;
    esp_err_t err = read_http_body(CONFIG_OTA_MANIFEST_URL, body,
                                   OTA_MANIFEST_MAX_SIZE);
    if (err == ESP_OK) err = parse_manifest(body, manifest);
    free(body);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Manifest request or validation failed");
        return err;
    }
    err = verify_manifest(manifest);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Chunk signature verification failed; skipping download");
        free_manifest(manifest);
    }
    return err;
}

static esp_err_t install_encrypted_firmware(const ota_manifest_t *manifest)
{
    const esp_partition_t *update_partition = esp_ota_get_next_update_partition(NULL);
    if (!update_partition || manifest->size > update_partition->size) return ESP_FAIL;

    esp_http_client_config_t config = {
        .url = CONFIG_OTA_FIRMWARE_URL,
        .method = HTTP_METHOD_GET,
        .timeout_ms = 10000,
        .buffer_size = OTA_DOWNLOAD_BUFFER_SIZE,
        .buffer_size_tx = 1024,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) return ESP_ERR_NO_MEM;
    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        close_http_client(client);
        return err;
    }
    int content_length = esp_http_client_fetch_headers(client);
    if (esp_http_client_get_status_code(client) != 200 ||
        content_length != (int)manifest->encrypted_size) {
        ESP_LOGE(TAG, "Encrypted size error: received %d, expected %zu",
                 content_length, manifest->encrypted_size);
        close_http_client(client);
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Downloading %zu encrypted bytes in %zu chunks",
             manifest->encrypted_size, manifest->chunk_count);

    psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
    psa_set_key_type(&attributes, PSA_KEY_TYPE_AES);
    psa_set_key_bits(&attributes, 256);
    psa_set_key_usage_flags(&attributes, PSA_KEY_USAGE_DECRYPT);
    psa_set_key_algorithm(&attributes, PSA_ALG_GCM);
    mbedtls_svc_key_id_t key_id = MBEDTLS_SVC_KEY_ID_INIT;
    psa_status_t crypto_status = psa_import_key(&attributes, device_aes_key,
                                                sizeof(device_aes_key), &key_id);
    psa_reset_key_attributes(&attributes);
    if (crypto_status != PSA_SUCCESS) {
        close_http_client(client);
        return ESP_FAIL;
    }

    uint8_t *encrypted_buffer = malloc(OTA_CHUNK_SIZE + OTA_GCM_TAG_SIZE);
    uint8_t *plain_buffer = malloc(OTA_CHUNK_SIZE);
    if (!encrypted_buffer || !plain_buffer) {
        free(encrypted_buffer);
        free(plain_buffer);
        psa_destroy_key(key_id);
        close_http_client(client);
        return ESP_ERR_NO_MEM;
    }

    esp_ota_handle_t ota_handle = 0;
    bool ota_started = false;
    bool hash_started = false;
    psa_hash_operation_t overall_hash = PSA_HASH_OPERATION_INIT;
    err = esp_ota_begin(update_partition, OTA_WITH_SEQUENTIAL_WRITES, &ota_handle);
    if (err != ESP_OK) goto cleanup;
    ota_started = true;
    if (psa_hash_setup(&overall_hash, PSA_ALG_SHA_256) != PSA_SUCCESS) {
        err = ESP_FAIL;
        goto cleanup;
    }
    hash_started = true;

    size_t encrypted_read = 0;
    size_t plaintext_written = 0;
    for (size_t index = 0; index < manifest->chunk_count; ++index) {
        const ota_chunk_t *chunk = &manifest->chunks[index];
        size_t chunk_read = 0;
        while (chunk_read < chunk->encrypted_size) {
            size_t read_size = chunk->encrypted_size - chunk_read;
            if (read_size > OTA_DOWNLOAD_BUFFER_SIZE) read_size = OTA_DOWNLOAD_BUFFER_SIZE;
            int bytes_read = esp_http_client_read(client,
                                                   (char *)encrypted_buffer + chunk_read,
                                                   read_size);
            if (bytes_read <= 0) {
                err = ESP_FAIL;
                goto cleanup;
            }
            chunk_read += (size_t)bytes_read;
        }
        encrypted_read += chunk_read;
        ESP_LOGI(TAG, "Chunk %zu/%zu downloaded (%zu encrypted bytes)",
             index + 1, manifest->chunk_count, chunk_read);

        size_t plain_length = 0;
        crypto_status = psa_aead_decrypt(key_id, PSA_ALG_GCM, chunk->nonce,
                                         sizeof(chunk->nonce), NULL, 0,
                                         encrypted_buffer, chunk->encrypted_size,
                                         plain_buffer, OTA_CHUNK_SIZE, &plain_length);
        if (crypto_status != PSA_SUCCESS || plain_length != chunk->size) {
            err = ESP_FAIL;
            goto cleanup;
        }
        ESP_LOGI(TAG, "Chunk %zu/%zu AES-GCM authentication passed",
                 index + 1, manifest->chunk_count);

        uint8_t chunk_digest[32];
        size_t chunk_digest_length = 0;
        psa_hash_operation_t chunk_hash = PSA_HASH_OPERATION_INIT;
        psa_status_t hash_status = psa_hash_setup(&chunk_hash, PSA_ALG_SHA_256);
        if (hash_status == PSA_SUCCESS) {
            hash_status = psa_hash_update(&chunk_hash, plain_buffer, plain_length);
        }
        if (hash_status == PSA_SUCCESS) {
            hash_status = psa_hash_finish(&chunk_hash, chunk_digest,
                                          sizeof(chunk_digest), &chunk_digest_length);
        } else {
            psa_hash_abort(&chunk_hash);
        }
        if (hash_status != PSA_SUCCESS || chunk_digest_length != sizeof(chunk_digest) ||
            memcmp(chunk_digest, chunk->sha256, sizeof(chunk_digest)) != 0) {
            err = ESP_FAIL;
            goto cleanup;
        }
        ESP_LOGI(TAG, "Chunk %zu/%zu SHA-256 verified",
                 index + 1, manifest->chunk_count);

        if (psa_hash_update(&overall_hash, plain_buffer, plain_length) != PSA_SUCCESS) {
            err = ESP_FAIL;
            goto cleanup;
        }
        err = esp_ota_write(ota_handle, plain_buffer, plain_length);
        if (err != ESP_OK) goto cleanup;
        plaintext_written += plain_length;
        ESP_LOGI(TAG, "Chunk %zu/%zu written to OTA partition (%zu/%zu bytes)",
             index + 1, manifest->chunk_count, plaintext_written, manifest->size);
    }

    uint8_t digest[32];
    size_t digest_length = 0;
    if (encrypted_read != manifest->encrypted_size ||
        plaintext_written != manifest->size ||
        !esp_http_client_is_complete_data_received(client) ||
        psa_hash_finish(&overall_hash, digest, sizeof(digest), &digest_length) != PSA_SUCCESS ||
        digest_length != sizeof(digest) || memcmp(digest, manifest->sha256, sizeof(digest)) != 0) {
        err = ESP_FAIL;
        goto cleanup;
    }
    ESP_LOGI(TAG, "Full firmware SHA-256 verified (%zu bytes)", plaintext_written);
    hash_started = false;
    close_http_client(client);
    client = NULL;
    free(encrypted_buffer);
    free(plain_buffer);
    psa_destroy_key(key_id);

    err = esp_ota_end(ota_handle);
    ota_started = false;
    if (err != ESP_OK) return err;
    err = esp_ota_set_boot_partition(update_partition);
    if (err != ESP_OK) return err;
    ESP_LOGI(TAG, "Encrypted firmware installed successfully. Restarting...");
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
    return ESP_OK;

cleanup:
    if (hash_started) psa_hash_abort(&overall_hash);
    if (ota_started) esp_ota_abort(ota_handle);
    free(encrypted_buffer);
    free(plain_buffer);
    psa_destroy_key(key_id);
    close_http_client(client);
    ESP_LOGE(TAG, "Chunked firmware verification or installation failed");
    return err;
}

static void ota_check_once(void)
{
    ota_manifest_t manifest;
    ESP_LOGI(TAG, "----------------------------------------");
    ESP_LOGI(TAG, "Checking signed firmware release; current: %s", current_version);

    if (fetch_and_verify_manifest(&manifest) != ESP_OK) return;
    ESP_LOGI(TAG, "Verified release: %s (%zu bytes)", manifest.version, manifest.size);
    if (!version_is_newer(manifest.version, current_version)) {
        ESP_LOGI(TAG, "No newer release available.");
        free_manifest(&manifest);
        return;
    }
    ESP_LOGI(TAG, "New version available: %s -> %s",
             current_version, manifest.version);
    if (install_encrypted_firmware(&manifest) != ESP_OK) {
        ESP_LOGW(TAG, "Firmware installation failed; keeping current firmware.");
    }
    free_manifest(&manifest);
}

static void ota_check_task(void *arg)
{
    (void)arg;
    while (1) {
        ota_check_once();
        vTaskDelay(pdMS_TO_TICKS(OTA_CHECK_INTERVAL_SECONDS * 1000));
    }
}

esp_err_t ota_manager_init(const char *firmware_version)
{
    if (!firmware_version || firmware_version[0] == '\0') return ESP_ERR_INVALID_ARG;
    if (psa_crypto_init() != PSA_SUCCESS || load_device_aes_key() != ESP_OK) {
        ESP_LOGE(TAG, "Per-device AES key is unavailable; OTA disabled");
        return ESP_ERR_INVALID_STATE;
    }
    snprintf(current_version, sizeof(current_version), "%s", firmware_version);
    ESP_LOGI(TAG, "Initializing OTA manager; current version: %s", current_version);
    if (xTaskCreate(ota_check_task, "ota_check_task", 8192, NULL, 5, NULL) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create OTA check task");
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
