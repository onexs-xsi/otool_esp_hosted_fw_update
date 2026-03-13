#include "otool_esp_hosted_fw_update.h"

#include "otool_esp_hosted_fw_update_config.h"
#include "otool_esp_hosted_fw_update_registry.h"

extern "C" {
#include "esp_hosted.h"
}

#include "esp_log.h"

static const char *TAG = "otool_esp_hosted_fw_update";

static bool otool_esp_hosted_fw_activate_supported(const esp_hosted_coprocessor_fwver_t &slave_version)
{
    return (slave_version.major1 > 2U) ||
           (slave_version.major1 == 2U && slave_version.minor1 > 5U);
}

static esp_err_t otool_esp_hosted_fw_check_version_compatibility(esp_hosted_coprocessor_fwver_t *slave_version)
{
    if (slave_version == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    *slave_version = {};
    esp_err_t ret = static_cast<esp_err_t>(esp_hosted_get_coprocessor_fwversion(slave_version));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read co-processor version: %s", esp_err_to_name(ret));
        return ret;
    }

    const uint32_t host_version = ESP_HOSTED_VERSION_VAL(
        ESP_HOSTED_VERSION_MAJOR_1,
        ESP_HOSTED_VERSION_MINOR_1,
        ESP_HOSTED_VERSION_PATCH_1);
    const uint32_t slave_version_value = ESP_HOSTED_VERSION_VAL(
        slave_version->major1,
        slave_version->minor1,
        slave_version->patch1);
    const uint32_t host_major_minor = host_version & 0xFFFFFF00U;
    const uint32_t slave_major_minor = slave_version_value & 0xFFFFFF00U;

    ESP_LOGI(TAG, "Host firmware version: " ESP_HOSTED_VERSION_PRINTF_FMT,
             ESP_HOSTED_VERSION_PRINTF_ARGS(host_version));
    ESP_LOGI(TAG, "Co-processor version: %u.%u.%u",
             static_cast<unsigned int>(slave_version->major1),
             static_cast<unsigned int>(slave_version->minor1),
             static_cast<unsigned int>(slave_version->patch1));

    if (host_major_minor > slave_major_minor) {
        ESP_LOGW(TAG, "Host major.minor is newer than co-processor; OTA may hang during esp_hosted_slave_ota_begin or later RPC steps");
        ESP_LOGW(TAG, "Continuing because version mismatch is configured as warning-only");
    }

    return ESP_OK;
}

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
    esp_hosted_coprocessor_fwver_t slave_version = {};
    otool_esp_hosted_fw_adjust_image(&blob, &fw_start, &fw_size);

    if (fw_size == 0) {
        return ESP_ERR_INVALID_SIZE;
    }

    ret = otool_esp_hosted_fw_check_version_compatibility(&slave_version);
    if (ret != ESP_OK) {
        return ret;
    }

    ESP_LOGI(TAG, "Start OTA: %s, size=%u", blob.name, (unsigned int) fw_size);

    ESP_LOGI(TAG, "Calling transport begin...");
    ret = transport_->begin();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "transport begin failed: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "transport begin OK, writing %u bytes in %u-byte chunks",
             (unsigned int) fw_size,
             (unsigned int) CONFIG_OTOOL_ESP_HOSTED_FW_CHUNK_SIZE);

    size_t offset = 0;
    uint32_t chunk_count = 0;
    while (offset < fw_size) {
        size_t bytes_to_send = (fw_size - offset > CONFIG_OTOOL_ESP_HOSTED_FW_CHUNK_SIZE) ? CONFIG_OTOOL_ESP_HOSTED_FW_CHUNK_SIZE : (fw_size - offset);
        ret = transport_->write(fw_start + offset, bytes_to_send);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "transport write failed at offset %u: %s", (unsigned int) offset, esp_err_to_name(ret));
            (void) transport_->end();
            return ret;
        }
        offset += bytes_to_send;
        chunk_count++;
        if ((chunk_count % 10U) == 0U) {
            ESP_LOGI(TAG, "Progress: %u/%u bytes (%u%%)",
                     (unsigned int) offset, (unsigned int) fw_size,
                     (unsigned int) (offset * 100U / fw_size));
        }
    }

    ESP_LOGI(TAG, "Write done, calling transport end...");
    ret = transport_->end();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "transport end failed: %s", esp_err_to_name(ret));
        return ret;
    }

    if (!otool_esp_hosted_fw_activate_supported(slave_version)) {
        ESP_LOGW(TAG, "Skipping transport activate: co-processor firmware %u.%u.%u does not support OTA activate RPC (requires v2.6.0+)",
                 static_cast<unsigned int>(slave_version.major1),
                 static_cast<unsigned int>(slave_version.minor1),
                 static_cast<unsigned int>(slave_version.patch1));
        ESP_LOGW(TAG, "OTA image transfer completed; reboot the co-processor or the board to let the new firmware take effect if supported by the slave boot flow");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Calling transport activate...");
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

