#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#define AMULET_RESPONSE_CONFIG_VERSION 2
#define AMULET_RESPONSE_MAX_STEPS 8
#define AMULET_RESPONSE_MIN_FREQUENCY_HZ 262
#define AMULET_RESPONSE_MAX_FREQUENCY_HZ 4186
#define AMULET_RESPONSE_DEFAULT_NOTE_DURATION_MS 330
#define AMULET_RESPONSE_MIN_NOTE_DURATION_MS 100
#define AMULET_RESPONSE_MAX_NOTE_DURATION_MS 1000
#define AMULET_RESPONSE_MAX_START_DELAY_MS 1000
#define AMULET_RESPONSE_TIMING_STEP_MS 10
/* A step at this frequency is a rest: the buzzer is silent, but the step's
 * light still shows for the note duration. */
#define AMULET_RESPONSE_REST_FREQUENCY_HZ 0

typedef enum {
    AMULET_CONFIG_RESPONSE,
    AMULET_CONFIG_CALL,
    AMULET_CONFIG_COUNT,
} amulet_config_target_t;

typedef struct {
    uint16_t frequency_hz;
    uint8_t red;
    uint8_t green;
    uint8_t blue;
    uint8_t reserved;
} amulet_response_step_t;

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint8_t step_count;
    uint8_t reserved;
    uint16_t note_duration_ms;
    uint16_t start_delay_ms;
    amulet_response_step_t steps[AMULET_RESPONSE_MAX_STEPS];
} amulet_response_config_t;

esp_err_t amulet_config_init(void);
void amulet_config_get(
    amulet_config_target_t target,
    amulet_response_config_t *config
);
void amulet_config_set_defaults(
    amulet_config_target_t target,
    amulet_response_config_t *config
);
bool amulet_config_is_valid(const amulet_response_config_t *config);
esp_err_t amulet_config_save(
    amulet_config_target_t target,
    const amulet_response_config_t *config
);
esp_err_t amulet_config_reset(amulet_config_target_t target);
// Stores a per-board default that Restore default (and boot without a saved
// pattern) returns to, and makes it the current pattern.
esp_err_t amulet_config_set_factory_default(
    amulet_config_target_t target,
    const amulet_response_config_t *config
);
