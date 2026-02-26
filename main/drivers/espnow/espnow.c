



#include "espnow.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_now.h"


esp_err_t espnow_init(void)
{
	esp_err_t err = esp_now_init();
	if (err != ESP_OK) {
		ESP_LOGW(TAG, "ESP-NOW initialize failed: %s", esp_err_to_name(err));
	}

	err = esp_now_register_recv_cb(espnow_recv_cb);
	if (err != ESP_OK) {
		ESP_LOGE(TAG, "ESP-NOW register receive callback failed: %s", esp_err_to_name(err));
		return err;
	}

	ESP_LOGI(TAG, "ESP-NOW initialized");
	return ESP_OK;
}


esp_err_t espnow_deinit(void)
{
	esp_err_t err = esp_now_deinit();
	if (err != ESP_OK) {
		ESP_LOGE(TAG, "ESP-NOW deinitialize failed: %s", esp_err_to_name(err));
		return err;
	}
	ESP_LOGI(TAG, "ESP-NOW deinitialized");
	return ESP_OK;
}

esp_err_t init_wifi_for_espnow(int tx_power_dbm)
{
    esp_err_t err = ESP_OK;
    err |= esp_netif_init();
    err |= esp_event_loop_create_default();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize network interface or event loop: %s", esp_err_to_name(err));
        return err;
    }


    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    err |= esp_wifi_init(&cfg);
    err |= esp_wifi_set_mode(WIFI_MODE_STA);
    err |= esp_wifi_start();
    err |= esp_wifi_set_channel(s_current_channel, WIFI_SECOND_CHAN_NONE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize Wi-Fi for ESP-NOW: %s", esp_err_to_name(err));
        return err;
    }

    if (tx_power_dbm > 0) {
        err = esp_wifi_set_max_tx_power(tx_power_dbm);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "Wi-Fi TX power limited to %d dBm", tx_power_dbm);
        } else {
            ESP_LOGW(TAG, "Failed to set Wi-Fi TX power: %s", esp_err_to_name(err));
            return err;
        }
    }
    return err;
}