



static esp_err_t save_channel_to_nvs(uint8_t ch)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_set_u8(h, NVS_KEY_CHANNEL, ch);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    if (err == ESP_OK) ESP_LOGI(TAG, "Saved channel %d to NVS", ch);
    else ESP_LOGW(TAG, "Failed to save channel to NVS: %s", esp_err_to_name(err));
    return err;
}