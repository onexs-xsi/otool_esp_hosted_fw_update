#include "otool_esp_hosted_fw_update.h"

#include "otool_esp_hosted_fw_update_config.h"
#include "otool_esp_hosted_fw_update_registry.h"

#include "esp_log.h"

static const char *TAG = "otool_esp_hosted_fw_update";

static void otool_esp_hosted_fw_adjust_image(const otool_esp_hosted_fw_blob_t *blob, const uint8_t **fw_start, size_t *fw_size)
{
    *fw_start = blob->start;
    *fw_size = (size_t) (blob->end - blob->start);

    if (*fw_size > 0x10000 && (*fw_start)[0] == 0xE9 && (*fw_start)[0x8000] == 0xAA && (*fw_start)[0x8001] == 0x50) {
        ESP_LOGW(TAG, "Detected merged binary, skip first 64KB");
        *fw_start += 0x10000;
        *fw_size -= 0x10000;
    }
}

OtoolEspHostedFwUpdater::OtoolEspHostedFwUpdater(OtoolEspHostedFwProvider *provider, OtoolEspHostedFwTransport *transport)
    : provider_(provider), transport_(transport)
{
}

esp_err_t OtoolEspHostedFwUpdater::update_by_name(const char *firmware_name)
{
    if (provider_ == nullptr || transport_ == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }

    if (firmware_name == nullptr || firmware_name[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    otool_esp_hosted_fw_blob_t blob = {};
    esp_err_t ret = provider_->get_blob(firmware_name, blob);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Firmware not found: %s", firmware_name);
        return ret;
    }

    const uint8_t *fw_start = nullptr;
    size_t fw_size = 0;
    otool_esp_hosted_fw_adjust_image(&blob, &fw_start, &fw_size);

    if (fw_size == 0) {
        return ESP_ERR_INVALID_SIZE;
    }

    ESP_LOGI(TAG, "Start OTA: %s, size=%u", blob.name, (unsigned int) fw_size);

    ret = transport_->begin();
    if (ret != ESP_OK) {
        return ret;
    }

    size_t offset = 0;
    uint32_t chunk_count = 0;
    while (offset < fw_size) {
        size_t bytes_to_send = (fw_size - offset > CONFIG_OTOOL_ESP_HOSTED_FW_CHUNK_SIZE) ? CONFIG_OTOOL_ESP_HOSTED_FW_CHUNK_SIZE : (fw_size - offset);
        ret = transport_->write(fw_start + offset, bytes_to_send);
        if (ret != ESP_OK) {
            (void) transport_->end();
            return ret;
        }
        offset += bytes_to_send;
        chunk_count++;
        if ((chunk_count % 50U) == 0U) {
            ESP_LOGI(TAG, "Progress: %u/%u", (unsigned int) offset, (unsigned int) fw_size);
        }
    }

    ret = transport_->end();
    if (ret != ESP_OK) {
        return ret;
    }

    return transport_->activate();
}

const char *OtoolEspHostedFwUpdater::version() const
{
    return "0.1.0-skeleton";
}

OtoolEspHostedFwProvider *OtoolEspHostedFwDefaultFactory::provider()
{
    return otool_esp_hosted_fw_registry_provider();
}

OtoolEspHostedFwTransport *OtoolEspHostedFwDefaultFactory::transport()
{
    return otool_esp_hosted_fw_registry_transport();
}

OtoolEspHostedFwUpdater *OtoolEspHostedFwDefaultFactory::updater()
{
    static OtoolEspHostedFwUpdater updater(provider(), transport());
    return &updater;
}

extern "C" esp_err_t otool_esp_hosted_fw_update_by_name(const char *firmware_name)
{
    return OtoolEspHostedFwDefaultFactory::updater()->update_by_name(firmware_name);
}

extern "C" const char *otool_esp_hosted_fw_update_version(void)
{
    return OtoolEspHostedFwDefaultFactory::updater()->version();
}

