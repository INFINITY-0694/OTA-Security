#include "ota_manager.h"
#include <stdbool.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"

#define OTA_CHECK_INTERVAL_SECONDS 30
#define OTA_HTTP_BUFFER_SIZE 128

static const char *TAG = "OTA";
static char current_version[OTA_HTTP_BUFFER_SIZE];
static char server_version[OTA_HTTP_BUFFER_SIZE];

static bool parse_version(const char *version, int *major, int *minor, int *patch)
{
    char extra;
    int values_read = sscanf(version, "%d.%d.%d%c", major, minor, patch, &extra);

    if (values_read != 3) {
        return false;
    }

    return *major >= 0 && *minor >= 0 && *patch >= 0;
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

    if (candidate_major != running_major) {
        return candidate_major > running_major;
    }
    if (candidate_minor != running_minor) {
        return candidate_minor > running_minor;
    }
    return candidate_patch > running_patch;
}

static esp_err_t install_firmware(void)
{
    const esp_partition_t *update_partition =
        esp_ota_get_next_update_partition(NULL);
    if (!update_partition) {
        ESP_LOGE(TAG, "No OTA update partition available; check the flashed partition table");
        return ESP_FAIL;
    }

    esp_http_client_config_t config = {
        .url = CONFIG_OTA_FIRMWARE_URL,
        .method = HTTP_METHOD_GET,
        .timeout_ms = 10000,
        .buffer_size = 4096,
        .buffer_size_tx = 1024,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Downloading firmware from: %s", CONFIG_OTA_FIRMWARE_URL);
    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Firmware connection failed: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return err;
    }

    int content_length = esp_http_client_fetch_headers(client);
    int status = esp_http_client_get_status_code(client);
    if (status != 200 || content_length <= 0) {
        ESP_LOGE(TAG, "Invalid firmware response: HTTP %d, size %d",
                 status, content_length);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Writing %d bytes to partition %s", content_length,
             update_partition->label);
    esp_ota_handle_t ota_handle = 0;
    err = esp_ota_begin(update_partition, content_length, &ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "OTA begin failed: %s", esp_err_to_name(err));
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return err;
    }

    uint8_t buffer[4096];
    int total_written = 0;
    while (total_written < content_length) {
        int bytes_read = esp_http_client_read(client, (char *)buffer,
                                               sizeof(buffer));
        if (bytes_read <= 0) {
            ESP_LOGE(TAG, "Firmware download ended at %d/%d bytes",
                     total_written, content_length);
            esp_ota_abort(ota_handle);
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
            return ESP_FAIL;
        }

        err = esp_ota_write(ota_handle, buffer, bytes_read);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "OTA write failed: %s", esp_err_to_name(err));
            esp_ota_abort(ota_handle);
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
            return err;
        }
        total_written += bytes_read;
    }

    if (total_written != content_length ||
        !esp_http_client_is_complete_data_received(client)) {
        ESP_LOGE(TAG, "Incomplete firmware download: received %d/%d bytes",
                 total_written, content_length);
        esp_ota_abort(ota_handle);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return ESP_FAIL;
    }

    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    err = esp_ota_end(ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "OTA image validation failed: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_ota_set_boot_partition(update_partition);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to select new boot partition: %s",
                 esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "Firmware installed successfully. Restarting...");
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
    return ESP_OK;
}

static esp_err_t http_get_version(void)
{
    memset(server_version, 0, sizeof(server_version));

    esp_http_client_config_t config = {
        .url = CONFIG_OTA_VERSION_URL,
        .method = HTTP_METHOD_GET,
        .timeout_ms = 5000,
        .buffer_size = 1024,
        .buffer_size_tx = 1024,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) return ESP_ERR_NO_MEM;

    ESP_LOGI(TAG, "Connecting to: %s", CONFIG_OTA_VERSION_URL);

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP connection failed: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return err;
    }

    int content_length = esp_http_client_fetch_headers(client);
    if (content_length < 0) {
        ESP_LOGE(TAG, "Failed to fetch HTTP headers");
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return ESP_FAIL;
    }

    int status = esp_http_client_get_status_code(client);
    ESP_LOGI(TAG, "HTTP status: %d", status);

    if (status != 200) {
        ESP_LOGE(TAG, "Server returned HTTP %d", status);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return ESP_FAIL;
    }

    int total_read = 0;
    while (total_read < (int)sizeof(server_version) - 1) {
        int n = esp_http_client_read(client, server_version + total_read,
                                     sizeof(server_version) - 1 - total_read);
        if (n < 0) {
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
            return ESP_FAIL;
        }
        if (n == 0) break;
        total_read += n;
    }
    server_version[total_read] = '\0';

    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    for (int i = total_read - 1; i >= 0; --i) {
        if (isspace((unsigned char)server_version[i])) server_version[i] = '\0';
        else break;
    }

    return strlen(server_version) ? ESP_OK : ESP_FAIL;
}

static void ota_check_once(void)
{
    ESP_LOGI(TAG, "----------------------------------------");
    ESP_LOGI(TAG, "Checking for firmware update...");
    ESP_LOGI(TAG, "Current firmware: %s", current_version);

    if (http_get_version() != ESP_OK) {
        ESP_LOGW(TAG, "Update check failed; continuing current firmware.");
        return;
    }

    ESP_LOGI(TAG, "Server firmware:  %s", server_version);

    if (strcmp(current_version, server_version) == 0) {
        ESP_LOGI(TAG, "No update available.");
    } else if (version_is_newer(server_version, current_version)) {
        ESP_LOGW(TAG, "NEW VERSION DETECTED!");
        ESP_LOGW(TAG, "Current: %s | Server: %s",
                 current_version, server_version);
        if (install_firmware() != ESP_OK) {
            ESP_LOGW(TAG, "Firmware installation failed; keeping current firmware.");
        }
    } else {
        ESP_LOGW(TAG, "Server version is not newer or has an invalid format.");
    }
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
    if (!firmware_version || firmware_version[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    snprintf(current_version, sizeof(current_version), "%s", firmware_version);

    ESP_LOGI(TAG, "Initializing OTA manager...");
    ESP_LOGI(TAG, "Current firmware version: %s", current_version);
    ESP_LOGI(TAG, "OTA check interval: %d seconds", OTA_CHECK_INTERVAL_SECONDS);

    if (xTaskCreate(ota_check_task, "ota_check_task", 8192, NULL, 5, NULL) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create OTA check task");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "OTA manager initialized.");
    return ESP_OK;
}
