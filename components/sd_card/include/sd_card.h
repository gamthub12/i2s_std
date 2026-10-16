#pragma once

#include <stdbool.h>
#include "esp_err.h"

/**
 * @brief Initialize and mount the SD Card filesystem.
 * 
 * @return esp_err_t ESP_OK on success, or error code on failure.
 */
esp_err_t sd_card_mount(void);

/**
 * @brief Unmount the SD Card filesystem.
 * 
 * @return esp_err_t ESP_OK on success, or error code on failure.
 */
esp_err_t sd_card_unmount(void);

/**
 * @brief Run a test write and read on the SD Card.
 */
void sd_card_test(void);

/**
 * @brief Initialize the SD card background monitor task (supports hot-plugging).
 */
void sd_card_init(void);

/**
 * @brief Lock SD filesystem access while the card is mounted.
 * @return true when the lock is held and the filesystem is available.
 */
bool sd_card_lock(void);

/** @brief Release a lock acquired by sd_card_lock(). */
void sd_card_unlock(void);

