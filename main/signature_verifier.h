#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

esp_err_t signature_verifier_verify(const uint8_t *data,
                                    size_t data_size,
                                    const uint8_t *signature,
                                    size_t signature_size);
