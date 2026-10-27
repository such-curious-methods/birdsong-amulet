#ifndef AMULET_AMBIENT_RUNTIME_H
#define AMULET_AMBIENT_RUNTIME_H

#include <stddef.h>
#include <stdint.h>

#include "amulet_ble_classifier.h"
#include "esp_err.h"

esp_err_t amulet_ambient_runtime_init(void);

void amulet_ambient_submit(
    const amulet_ble_ambient_marker_t *marker,
    const uint8_t address[6],
    uint8_t address_type,
    int8_t rssi,
    int64_t seen_us
);

#endif
