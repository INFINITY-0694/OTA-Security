#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_app_desc.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "wifi_manager.h"
#include "ota_manager.h"

static const char *TAG = "MAIN";

void app_main(void)
{
    const esp_app_desc_t *app = esp_app_get_description();

    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "       SECURE OTA PROJECT");
    ESP_LOGI(TAG, "       By : Divy Soni,Kalp Patel");
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "ESP-IDF application started");
    ESP_LOGI(TAG, "Firmware version: %s", app->version);
    ESP_LOGI(TAG, "Build date: %s %s", app->date, app->time);
    ESP_LOGI(TAG, "========================================");

    ESP_LOGI(TAG, "[BOOT] Initializing NVS...");
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "[NVS] Erasing and reinitializing NVS...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_LOGI(TAG, "[NVS] NVS initialized successfully.");

    ESP_LOGI(TAG, "[WIFI] Starting hardware Wi-Fi manager...");
    ret = wifi_manager_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "[WIFI] Initialization failed.");
        while (1) vTaskDelay(pdMS_TO_TICKS(1000));
    }

    ESP_LOGI(TAG, "[OTA] Starting OTA manager...");
    ret = ota_manager_init(app->version);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "[OTA] Initialization failed.");
        while (1) vTaskDelay(pdMS_TO_TICKS(1000));
    }

    ESP_LOGI(TAG, "[SYSTEM] V1 initialization complete.");

    while (1) vTaskDelay(pdMS_TO_TICKS(10000));
}
