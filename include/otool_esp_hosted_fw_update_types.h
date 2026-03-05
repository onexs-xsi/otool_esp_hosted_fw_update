#pragma once

#include "esp_err.h"
#include <stddef.h>
#include <stdint.h>

typedef struct {
    const char *name;
    const uint8_t *start;
    const uint8_t *end;
} otool_esp_hosted_fw_blob_t;

class OtoolEspHostedFwProvider {
public:
    virtual ~OtoolEspHostedFwProvider() = default;
    virtual esp_err_t get_blob(const char *firmware_name, otool_esp_hosted_fw_blob_t &blob) = 0;
};

class OtoolEspHostedFwTransport {
public:
    virtual ~OtoolEspHostedFwTransport() = default;
    virtual esp_err_t begin() = 0;
    virtual esp_err_t write(const uint8_t *data, size_t len) = 0;
    virtual esp_err_t end() = 0;
    virtual esp_err_t activate() = 0;
};

class OtoolEspHostedFwStubProvider final : public OtoolEspHostedFwProvider {
public:
    esp_err_t get_blob(const char *firmware_name, otool_esp_hosted_fw_blob_t &blob) override;
};

class OtoolEspHostedFwEmbeddedProvider final : public OtoolEspHostedFwProvider {
public:
    esp_err_t get_blob(const char *firmware_name, otool_esp_hosted_fw_blob_t &blob) override;
};

class OtoolEspHostedFwStubTransport final : public OtoolEspHostedFwTransport {
public:
    esp_err_t begin() override;
    esp_err_t write(const uint8_t *data, size_t len) override;
    esp_err_t end() override;
    esp_err_t activate() override;
};

class OtoolEspHostedFwEspHostedTransport final : public OtoolEspHostedFwTransport {
public:
    esp_err_t begin() override;
    esp_err_t write(const uint8_t *data, size_t len) override;
    esp_err_t end() override;
    esp_err_t activate() override;
};

