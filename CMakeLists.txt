set(OTOOL_ESP_HOSTED_FW_UPDATE_SRCS
    "src/core/otool_esp_hosted_fw_update_core.cpp"
    "src/core/otool_esp_hosted_fw_update_registry.cpp"
    "src/providers/otool_esp_hosted_fw_provider_stub.cpp"
    "src/ports/espidf/otool_esp_hosted_fw_platform_espidf.cpp"
    "src/ports/espidf/otool_esp_hosted_fw_transport_esp_hosted.cpp"
    "src/ports/espidf/otool_esp_hosted_fw_update_minimal_init.cpp"
)

if(CONFIG_OTOOL_ESP_HOSTED_FW_PROVIDER_EMBEDDED)
    list(APPEND OTOOL_ESP_HOSTED_FW_UPDATE_SRCS "src/providers/otool_esp_hosted_fw_provider_embedded.cpp")
endif()

if(CONFIG_OTOOL_ESP_HOSTED_FW_ENABLE_COMPAT_API)
    list(APPEND OTOOL_ESP_HOSTED_FW_UPDATE_SRCS "src/compat/otool_esp_hosted_wifi_firmware_ota_compat.cpp")
endif()

set(OTOOL_ESP_HOSTED_FW_UPDATE_EMBED_FILES)
if(CONFIG_OTOOL_ESP_HOSTED_FW_PROVIDER_EMBEDDED)
    list(APPEND OTOOL_ESP_HOSTED_FW_UPDATE_EMBED_FILES
        "firmware/esp32c6_wifi_firmware/fw_tab5.bin"
        "firmware/esp32c6_wifi_firmware/fw_bluedroid.bin"
        "firmware/esp32c6_wifi_firmware/fw_nimble.bin"
        "firmware/esp32c6_wifi_firmware/fw_noble.bin"
    )
endif()

idf_component_register(
    SRCS ${OTOOL_ESP_HOSTED_FW_UPDATE_SRCS}
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "src" "src/core" "src/providers" "src/ports/espidf"
    PRIV_REQUIRES esp_hosted nvs_flash esp_event esp_netif esp_wifi
    EMBED_FILES ${OTOOL_ESP_HOSTED_FW_UPDATE_EMBED_FILES}
)
