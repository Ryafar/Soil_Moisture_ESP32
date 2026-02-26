



/**
 * @file nvs.c
 * @brief NVS (Non-Volatile Storage) Driver - Implementation
 *
 * Stores arbitrary structs as blobs so the driver is fully independent
 * of any specific data layout.
 */

#include "nvs.h"

#include "nvs_flash.h"
#include "esp_log.h"
#include <string.h>
#include <stdbool.h>

static const char *TAG = "NVS_DRIVER";

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

esp_err_t nvs_driver_init(void)
{
    esp_err_t err = nvs_flash_init();

    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition needs erase (%s), erasing and reinitializing...",
                 esp_err_to_name(err));
        err = nvs_flash_erase();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "NVS erase failed: %s", esp_err_to_name(err));
            return err;
        }
        err = nvs_flash_init();
    }

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS init failed: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "NVS initialized successfully");
    }

    return err;
}

esp_err_t nvs_driver_save(const char *ns, const char *key,
                          const void *data, size_t size)
{
    if (!ns || !key || !data || size == 0) {
        ESP_LOGE(TAG, "nvs_driver_save: invalid arguments");
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle;
    esp_err_t err = nvs_open(ns, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open('%s') failed: %s", ns, esp_err_to_name(err));
        return err;
    }

    err = nvs_set_blob(handle, key, data, size);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_set_blob('%s'/'%s') failed: %s", ns, key, esp_err_to_name(err));
        nvs_close(handle);
        return err;
    }

    err = nvs_commit(handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_commit('%s'/'%s') failed: %s", ns, key, esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "Saved %zu bytes to '%s'/'%s'", size, ns, key);
    }

    nvs_close(handle);
    return err;
}

esp_err_t nvs_driver_load(const char *ns, const char *key,
                          void *data, size_t size)
{
    if (!ns || !key || !data || size == 0) {
        ESP_LOGE(TAG, "nvs_driver_load: invalid arguments");
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle;
    esp_err_t err = nvs_open(ns, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        if (err == ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGD(TAG, "Namespace '%s' not found (first boot?)", ns);
        } else {
            ESP_LOGE(TAG, "nvs_open('%s') failed: %s", ns, esp_err_to_name(err));
        }
        return err;
    }

    size_t stored_size = size;
    err = nvs_get_blob(handle, key, data, &stored_size);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGD(TAG, "Key '%s'/'%s' not found in NVS (first boot?)", ns, key);
    } else if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_get_blob('%s'/'%s') failed: %s", ns, key, esp_err_to_name(err));
    } else if (stored_size != size) {
        ESP_LOGW(TAG, "Size mismatch for '%s'/'%s': stored=%zu, expected=%zu",
                 ns, key, stored_size, size);
        err = ESP_ERR_INVALID_SIZE;
    } else {
        ESP_LOGI(TAG, "Loaded %zu bytes from '%s'/'%s'", stored_size, ns, key);
    }

    nvs_close(handle);
    return err;
}

esp_err_t nvs_driver_erase_key(const char *ns, const char *key)
{
    if (!ns || !key) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle;
    esp_err_t err = nvs_open(ns, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open('%s') failed: %s", ns, esp_err_to_name(err));
        return err;
    }

    err = nvs_erase_key(handle, key);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGD(TAG, "Key '%s'/'%s' not found, nothing to erase", ns, key);
        err = ESP_OK;
    } else if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_erase_key('%s'/'%s') failed: %s", ns, key, esp_err_to_name(err));
    } else {
        nvs_commit(handle);
        ESP_LOGI(TAG, "Erased key '%s'/'%s'", ns, key);
    }

    nvs_close(handle);
    return err;
}

esp_err_t nvs_driver_erase_namespace(const char *ns)
{
    if (!ns) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle;
    esp_err_t err = nvs_open(ns, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open('%s') failed: %s", ns, esp_err_to_name(err));
        return err;
    }

    err = nvs_erase_all(handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_erase_all('%s') failed: %s", ns, esp_err_to_name(err));
    } else {
        nvs_commit(handle);
        ESP_LOGI(TAG, "Erased all keys in namespace '%s'", ns);
    }

    nvs_close(handle);
    return err;
}

bool nvs_driver_key_exists(const char *ns, const char *key)
{
    if (!ns || !key) {
        return false;
    }

    nvs_handle_t handle;
    if (nvs_open(ns, NVS_READONLY, &handle) != ESP_OK) {
        return false;
    }

    // Pass NULL as out_value to query the required size without reading data.
    // Returns ESP_OK if key exists.
    size_t required_size = 0;
    esp_err_t err = nvs_get_blob(handle, key, NULL, &required_size);
    nvs_close(handle);

    return (err == ESP_OK);
}