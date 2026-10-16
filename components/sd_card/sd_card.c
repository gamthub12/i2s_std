#include <stdio.h>
#include <string.h>
#include <sys/unistd.h>
#include <sys/stat.h>
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdmmc_host.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "sd_card.h"

static const char *TAG = "sd_card";
#define MOUNT_POINT "/sdcard"

// Pin configuration based on user's layout
#define PIN_NUM_CLK  39
#define PIN_NUM_CMD  38
#define PIN_NUM_D0   40

static sdmmc_card_t *s_card = NULL;
static bool s_mount_silent = false;
static SemaphoreHandle_t s_fs_mutex = NULL;

static void ensure_fs_mutex(void)
{
    if (s_fs_mutex == NULL) {
        s_fs_mutex = xSemaphoreCreateMutex();
        configASSERT(s_fs_mutex != NULL);
    }
}

bool sd_card_lock(void)
{
    ensure_fs_mutex();
    xSemaphoreTake(s_fs_mutex, portMAX_DELAY);
    if (s_card == NULL) {
        xSemaphoreGive(s_fs_mutex);
        return false;
    }
    return true;
}

void sd_card_unlock(void)
{
    if (s_fs_mutex != NULL) xSemaphoreGive(s_fs_mutex);
}

static esp_err_t sd_card_mount_unlocked(void)
{
    if (s_card != NULL) {
        if (!s_mount_silent) {
            ESP_LOGW(TAG, "SD Card already mounted");
        }
        return ESP_OK;
    }

    if (!s_mount_silent) {
        ESP_LOGI(TAG, "Initializing SD card (1-bit SDMMC)");
    }

    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        /* Never format user media automatically when mount fails. */
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024
    };

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.flags = SDMMC_HOST_FLAG_1BIT; // Configure host for 1-bit mode

    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
    slot_config.width = 1;
    slot_config.clk = PIN_NUM_CLK;
    slot_config.cmd = PIN_NUM_CMD;
    slot_config.d0 = PIN_NUM_D0;
    
    // Enable internal pullups. Note: external pullups (10k Ohm to 3.3V) are highly recommended on CMD and D0.
    slot_config.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    esp_log_level_t old_level_sdmmc = ESP_LOG_INFO;
    esp_log_level_t old_level_sd_card = ESP_LOG_INFO;
    if (s_mount_silent) {
        old_level_sdmmc = esp_log_level_get("sdmmc_common");
        old_level_sd_card = esp_log_level_get(TAG);
        esp_log_level_set("sdmmc_common", ESP_LOG_NONE);
        esp_log_level_set("sdmmc_cmd", ESP_LOG_NONE);
        esp_log_level_set("sdmmc_hal", ESP_LOG_NONE);
        esp_log_level_set("vfs_fat_sdmmc", ESP_LOG_NONE);
        esp_log_level_set(TAG, ESP_LOG_NONE);
    }

    esp_err_t ret = esp_vfs_fat_sdmmc_mount(MOUNT_POINT, &host, &slot_config, &mount_config, &s_card);

    if (s_mount_silent) {
        esp_log_level_set("sdmmc_common", old_level_sdmmc);
        esp_log_level_set("sdmmc_cmd", old_level_sdmmc);
        esp_log_level_set("sdmmc_hal", old_level_sdmmc);
        esp_log_level_set("vfs_fat_sdmmc", old_level_sdmmc);
        esp_log_level_set(TAG, old_level_sd_card);
    }

    if (ret != ESP_OK) {
        if (!s_mount_silent) {
            if (ret == ESP_ERR_TIMEOUT) {
                ESP_LOGE(TAG, "Failed to initialize SD card (Timeout / 0x107).");
                ESP_LOGE(TAG, "Please ensure CMD and D0 (DAT0) pins have external pull-up resistors (e.g. 10k Ohm to 3.3V) and the card is properly inserted.");
            } else {
                ESP_LOGE(TAG, "Failed to initialize SD card (%s).", esp_err_to_name(ret));
            }
        }
        s_card = NULL;
        return ret;
    }

    if (!s_mount_silent) {
        ESP_LOGI(TAG, "SD Card mounted successfully at %s", MOUNT_POINT);
        sdmmc_card_print_info(stdout, s_card);
    }
    return ESP_OK;
}

esp_err_t sd_card_mount(void)
{
    ensure_fs_mutex();
    xSemaphoreTake(s_fs_mutex, portMAX_DELAY);
    esp_err_t ret = sd_card_mount_unlocked();
    xSemaphoreGive(s_fs_mutex);
    return ret;
}

static esp_err_t sd_card_unmount_unlocked(void)
{
    if (s_card == NULL) {
        ESP_LOGW(TAG, "SD Card not mounted");
        return ESP_OK;
    }

    esp_err_t ret = esp_vfs_fat_sdcard_unmount(MOUNT_POINT, s_card);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Card unmounted successfully");
        s_card = NULL;
    } else {
        ESP_LOGE(TAG, "Failed to unmount card (%s)", esp_err_to_name(ret));
    }
    return ret;
}

esp_err_t sd_card_unmount(void)
{
    ensure_fs_mutex();
    xSemaphoreTake(s_fs_mutex, portMAX_DELAY);
    esp_err_t ret = sd_card_unmount_unlocked();
    xSemaphoreGive(s_fs_mutex);
    return ret;
}

static void sd_card_test_unlocked(void)
{
    const char *file_path = MOUNT_POINT "/.sd_card_check.tmp";
    ESP_LOGI(TAG, "Opening file %s for writing", file_path);
    FILE *f = fopen(file_path, "w");
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to open file for writing");
        return;
    }
    fprintf(f, "Hello ESP32-S3 SD Card!\n");
    fclose(f);
    ESP_LOGI(TAG, "File written successfully");

    ESP_LOGI(TAG, "Opening file %s for reading", file_path);
    f = fopen(file_path, "r");
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to open file for reading");
        unlink(file_path);
        return;
    }
    char line[64];
    if (fgets(line, sizeof(line), f) != NULL) {
        ESP_LOGI(TAG, "Read from file: %s", line);
    } else {
        ESP_LOGE(TAG, "Failed to read line from file");
    }
    fclose(f);
    unlink(file_path);
}

void sd_card_test(void)
{
    if (!sd_card_lock()) {
        ESP_LOGE(TAG, "Cannot run test, SD Card is not mounted");
        return;
    }
    sd_card_test_unlocked();
    sd_card_unlock();
}

static void sd_card_monitor_task(void *pvParameters)
{
    bool last_connected = false;
    ESP_LOGI(TAG, "SD Card Background Monitor Task started");

    while (1) {
        ensure_fs_mutex();
        xSemaphoreTake(s_fs_mutex, portMAX_DELAY);
        if (s_card != NULL) {
            // Card is mounted, verify if it is still physically present
            esp_err_t ret = sdmmc_get_status(s_card);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "Thẻ SD đã bị rút ra! (Phát hiện thời gian thực)");
                sd_card_unmount_unlocked();
                last_connected = false;
            } else {
                if (!last_connected) {
                    ESP_LOGI(TAG, "Thẻ SD đang hoạt động bình thường.");
                    last_connected = true;
                }
            }
        } else {
            // Card is not mounted, attempt a silent mount
            s_mount_silent = true;
            esp_err_t ret = sd_card_mount_unlocked();
            s_mount_silent = false;

            if (ret == ESP_OK) {
                ESP_LOGI(TAG, "Thẻ SD đã được cắm vào! (Phát hiện thời gian thực, tự động mount)");
                sdmmc_card_print_info(stdout, s_card);
                sd_card_test_unlocked();
                last_connected = true;
            }
        }
        xSemaphoreGive(s_fs_mutex);
        // Poll every 1.5 seconds
        vTaskDelay(pdMS_TO_TICKS(1500));
    }
}

void sd_card_init(void)
{
    ensure_fs_mutex();
    xTaskCreate(sd_card_monitor_task, "sd_card_monitor", 4096, NULL, 4, NULL);
}

