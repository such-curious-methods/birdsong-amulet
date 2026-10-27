#pragma once

#include <stdbool.h>

#include "amulet_config.h"
#include "esp_err.h"

typedef bool (*amulet_response_preview_fn)(
    amulet_config_target_t target,
    const amulet_response_config_t *response
);

esp_err_t amulet_serial_init(amulet_response_preview_fn preview_response);
