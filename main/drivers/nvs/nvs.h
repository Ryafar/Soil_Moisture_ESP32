/**
 * @file nvs.h
 * @brief NVS (Non-Volatile Storage) Driver
 *
 * Generic blob-based NVS driver. Any struct can be saved/loaded without
 * the driver needing to know its layout. The caller owns the struct definition
 * and simply passes a pointer + size.
 *
 * Typical usage:
 * @code
 *   // Define your config struct wherever you like
 *   typedef struct { float threshold; uint8_t interval; } app_config_t;
 *
 *   app_config_t cfg = { .threshold = 1.5f, .interval = 10 };
 *
 *   nvs_driver_init();
 *   nvs_driver_save("app", "config", &cfg, sizeof(cfg));
 *
 *   app_config_t loaded = {0};
 *   nvs_driver_load("app", "config", &loaded, sizeof(loaded));
 * @endcode
 */

#ifndef NVS_DRIVER_H
#define NVS_DRIVER_H

#include "esp_err.h"
#include <stddef.h>
#include <stdbool.h>

/**
 * @brief Initialize NVS flash storage.
 *
 * Must be called once at startup before any other nvs_driver_* call.
 * Handles the ESP_ERR_NVS_NO_FREE_PAGES / ESP_ERR_NVS_NEW_VERSION_FOUND
 * case by erasing and re-initializing automatically.
 *
 * @return ESP_OK on success, error code otherwise.
 */
esp_err_t nvs_driver_init(void);

/**
 * @brief Save a struct (or any data) to NVS as a blob.
 *
 * @param ns   NVS namespace string (max 15 chars).
 * @param key  Key string (max 15 chars).
 * @param data Pointer to data to save.
 * @param size Size in bytes of the data.
 * @return ESP_OK on success, error code otherwise.
 */
esp_err_t nvs_driver_save(const char *ns, const char *key,
                          const void *data, size_t size);

/**
 * @brief Load a struct (or any data) from NVS.
 *
 * @param ns   NVS namespace string (max 15 chars).
 * @param key  Key string (max 15 chars).
 * @param data Pointer to buffer that receives the data.
 * @param size Size in bytes of the buffer (must match saved size).
 * @return ESP_OK on success,
 *         ESP_ERR_NVS_NOT_FOUND if key has never been saved,
 *         error code otherwise.
 */
esp_err_t nvs_driver_load(const char *ns, const char *key,
                          void *data, size_t size);

/**
 * @brief Erase a single key from NVS.
 *
 * @param ns  NVS namespace string.
 * @param key Key string to erase.
 * @return ESP_OK on success, error code otherwise.
 */
esp_err_t nvs_driver_erase_key(const char *ns, const char *key);

/**
 * @brief Erase all keys in a namespace.
 *
 * @param ns NVS namespace string to wipe.
 * @return ESP_OK on success, error code otherwise.
 */
esp_err_t nvs_driver_erase_namespace(const char *ns);

/**
 * @brief Check whether a key exists in NVS.
 *
 * @param ns  NVS namespace string.
 * @param key Key string.
 * @return true if the key exists, false otherwise.
 */
bool nvs_driver_key_exists(const char *ns, const char *key);

#endif // NVS_DRIVER_H
