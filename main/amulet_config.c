#include "amulet_config.h"

#include <stddef.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs.h"

#define AMULET_CONFIG_MAGIC 0x414d4c54UL
#define AMULET_CONFIG_NAMESPACE "amulet"

static const char *TAG = "amulet_config";
static const char *CONFIG_NAMES[AMULET_CONFIG_COUNT] = {"response", "call"};
static const char *CONFIG_NVS_KEYS[AMULET_CONFIG_COUNT] = {"response", "call"};
// A per-board factory default, written when the fleet is flashed. Restore
// default and boot without a saved pattern use it instead of the built-in one.
static const char *FACTORY_NVS_KEYS[AMULET_CONFIG_COUNT] = {"response_def", "call_def"};
static SemaphoreHandle_t config_mutex;
static nvs_handle_t config_nvs;
static amulet_response_config_t current_configs[AMULET_CONFIG_COUNT];
static amulet_response_config_t factory_configs[AMULET_CONFIG_COUNT];
static bool factory_config_present[AMULET_CONFIG_COUNT];

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint8_t step_count;
    uint8_t reserved;
    amulet_response_step_t steps[AMULET_RESPONSE_MAX_STEPS];
} amulet_response_config_v1_t;

_Static_assert(sizeof(amulet_response_step_t) == 6, "response step layout changed");
_Static_assert(sizeof(amulet_response_config_v1_t) == 56, "v1 response layout changed");
_Static_assert(sizeof(amulet_response_config_t) == 60, "response config layout changed");

static bool target_is_valid(amulet_config_target_t target)
{
    return target >= AMULET_CONFIG_RESPONSE && target < AMULET_CONFIG_COUNT;
}

void amulet_config_set_defaults(
    amulet_config_target_t target,
    amulet_response_config_t *config
)
{
    *config = (amulet_response_config_t) {
        .magic = AMULET_CONFIG_MAGIC,
        .version = AMULET_RESPONSE_CONFIG_VERSION,
        .step_count = 4,
        .note_duration_ms = AMULET_RESPONSE_DEFAULT_NOTE_DURATION_MS,
        .start_delay_ms = 0,
        .steps = {
            {.frequency_hz = 523, .red = 0, .green = 170, .blue = 128},
            {.frequency_hz = 659, .red = 0, .green = 170, .blue = 128},
            {.frequency_hz = 784, .red = 0, .green = 170, .blue = 128},
            {.frequency_hz = 1047, .red = 0, .green = 170, .blue = 128},
        },
    };

    if (target == AMULET_CONFIG_CALL) {
        config->steps[0].frequency_hz = 1047;
        config->steps[1].frequency_hz = 1319;
        config->steps[2].frequency_hz = 1568;
        config->steps[3].frequency_hz = 2093;
    }
}

static bool step_frequency_is_valid(uint16_t frequency_hz)
{
    return frequency_hz == AMULET_RESPONSE_REST_FREQUENCY_HZ ||
        (frequency_hz >= AMULET_RESPONSE_MIN_FREQUENCY_HZ &&
         frequency_hz <= AMULET_RESPONSE_MAX_FREQUENCY_HZ);
}

bool amulet_config_is_valid(const amulet_response_config_t *config)
{
    if (config == NULL ||
        config->magic != AMULET_CONFIG_MAGIC ||
        config->version != AMULET_RESPONSE_CONFIG_VERSION ||
        config->step_count == 0 ||
        config->step_count > AMULET_RESPONSE_MAX_STEPS ||
        config->note_duration_ms < AMULET_RESPONSE_MIN_NOTE_DURATION_MS ||
        config->note_duration_ms > AMULET_RESPONSE_MAX_NOTE_DURATION_MS ||
        config->start_delay_ms > AMULET_RESPONSE_MAX_START_DELAY_MS ||
        config->note_duration_ms % AMULET_RESPONSE_TIMING_STEP_MS != 0 ||
        config->start_delay_ms % AMULET_RESPONSE_TIMING_STEP_MS != 0) {
        return false;
    }

    for (uint8_t index = 0; index < config->step_count; index++) {
        if (!step_frequency_is_valid(config->steps[index].frequency_hz)) {
            return false;
        }
    }

    return true;
}

static bool v1_config_is_valid(const amulet_response_config_v1_t *config)
{
    if (config->magic != AMULET_CONFIG_MAGIC ||
        config->version != 1 ||
        config->step_count == 0 ||
        config->step_count > AMULET_RESPONSE_MAX_STEPS) {
        return false;
    }

    for (uint8_t index = 0; index < config->step_count; index++) {
        uint16_t frequency_hz = config->steps[index].frequency_hz;
        if (frequency_hz < AMULET_RESPONSE_MIN_FREQUENCY_HZ ||
            frequency_hz > AMULET_RESPONSE_MAX_FREQUENCY_HZ) {
            return false;
        }
    }
    return true;
}

// The factory default when one is stored, otherwise the built-in pattern.
static void get_effective_default(
    amulet_config_target_t target,
    amulet_response_config_t *config
)
{
    if (factory_config_present[target]) {
        *config = factory_configs[target];
    } else {
        amulet_config_set_defaults(target, config);
    }
}

static esp_err_t load_factory_config(amulet_config_target_t target)
{
    amulet_response_config_t stored_config;
    size_t stored_size = sizeof(stored_config);
    esp_err_t error = nvs_get_blob(
        config_nvs,
        FACTORY_NVS_KEYS[target],
        &stored_config,
        &stored_size
    );
    if (error == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }
    if (error == ESP_ERR_NVS_INVALID_LENGTH ||
        (error == ESP_OK && (stored_size != sizeof(stored_config) ||
                             !amulet_config_is_valid(&stored_config)))) {
        ESP_LOGW(TAG, "stored factory %s is invalid; using the built-in default",
                 CONFIG_NAMES[target]);
        return ESP_OK;
    }
    if (error != ESP_OK) {
        return error;
    }

    factory_configs[target] = stored_config;
    factory_config_present[target] = true;
    ESP_LOGI(TAG, "loaded factory %s with %u step(s)",
             CONFIG_NAMES[target],
             (unsigned int)stored_config.step_count);
    return ESP_OK;
}

static esp_err_t load_config(amulet_config_target_t target)
{
    size_t stored_size = 0;
    esp_err_t error = nvs_get_blob(
        config_nvs,
        CONFIG_NVS_KEYS[target],
        NULL,
        &stored_size
    );
    if (error == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "using default local %s", CONFIG_NAMES[target]);
        return ESP_OK;
    }
    if (error != ESP_OK) {
        return error;
    }
    if (target == AMULET_CONFIG_RESPONSE &&
        stored_size == sizeof(amulet_response_config_v1_t)) {
        amulet_response_config_v1_t v1_config;
        error = nvs_get_blob(
            config_nvs,
            CONFIG_NVS_KEYS[target],
            &v1_config,
            &stored_size
        );
        if (error != ESP_OK) {
            return error;
        }
        if (!v1_config_is_valid(&v1_config)) {
            ESP_LOGW(TAG, "stored v1 response is invalid; using defaults");
            return ESP_OK;
        }

        amulet_response_config_t *current_config = &current_configs[target];
        current_config->step_count = v1_config.step_count;
        memcpy(current_config->steps, v1_config.steps, sizeof(current_config->steps));
        error = nvs_set_blob(
            config_nvs,
            CONFIG_NVS_KEYS[target],
            current_config,
            sizeof(*current_config)
        );
        if (error != ESP_OK) {
            return error;
        }
        error = nvs_commit(config_nvs);
        if (error != ESP_OK) {
            return error;
        }
        ESP_LOGI(TAG, "migrated v1 local response with %u step(s)",
                 (unsigned int)current_config->step_count);
        return ESP_OK;
    }
    if (stored_size != sizeof(current_configs[target])) {
        ESP_LOGW(TAG, "stored %s has unsupported size=%u; using defaults",
                  CONFIG_NAMES[target],
                  (unsigned int)stored_size);
        return ESP_OK;
    }

    amulet_response_config_t stored_config;
    error = nvs_get_blob(
        config_nvs,
        CONFIG_NVS_KEYS[target],
        &stored_config,
        &stored_size
    );
    if (error != ESP_OK) {
        return error;
    }
    if (!amulet_config_is_valid(&stored_config)) {
        ESP_LOGW(TAG, "stored %s is invalid or unsupported; using defaults",
                 CONFIG_NAMES[target]);
        return ESP_OK;
    }

    current_configs[target] = stored_config;
    ESP_LOGI(TAG, "loaded local %s with %u step(s)",
             CONFIG_NAMES[target],
             (unsigned int)current_configs[target].step_count);
    return ESP_OK;
}

esp_err_t amulet_config_init(void)
{
    config_mutex = xSemaphoreCreateMutex();
    if (config_mutex == NULL) {
        return ESP_ERR_NO_MEM;
    }

    esp_err_t error = nvs_open(AMULET_CONFIG_NAMESPACE, NVS_READWRITE, &config_nvs);
    if (error != ESP_OK) {
        return error;
    }

    for (int target = 0; target < AMULET_CONFIG_COUNT; target++) {
        error = load_factory_config((amulet_config_target_t)target);
        if (error != ESP_OK) {
            return error;
        }
        get_effective_default((amulet_config_target_t)target, &current_configs[target]);
    }

    error = load_config(AMULET_CONFIG_RESPONSE);
    if (error != ESP_OK) {
        return error;
    }
    return load_config(AMULET_CONFIG_CALL);
}

void amulet_config_get(
    amulet_config_target_t target,
    amulet_response_config_t *config
)
{
    if (!target_is_valid(target) || config == NULL) {
        return;
    }
    xSemaphoreTake(config_mutex, portMAX_DELAY);
    *config = current_configs[target];
    xSemaphoreGive(config_mutex);
}

esp_err_t amulet_config_save(
    amulet_config_target_t target,
    const amulet_response_config_t *config
)
{
    if (!target_is_valid(target) || !amulet_config_is_valid(config)) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t error = nvs_set_blob(
        config_nvs,
        CONFIG_NVS_KEYS[target],
        config,
        sizeof(*config)
    );
    if (error != ESP_OK) {
        return error;
    }
    error = nvs_commit(config_nvs);
    if (error != ESP_OK) {
        return error;
    }

    xSemaphoreTake(config_mutex, portMAX_DELAY);
    current_configs[target] = *config;
    xSemaphoreGive(config_mutex);
    ESP_LOGI(TAG, "saved local %s with %u step(s)", CONFIG_NAMES[target],
              (unsigned int)config->step_count);
    return ESP_OK;
}

esp_err_t amulet_config_reset(amulet_config_target_t target)
{
    if (!target_is_valid(target)) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t error = nvs_erase_key(config_nvs, CONFIG_NVS_KEYS[target]);
    if (error != ESP_OK && error != ESP_ERR_NVS_NOT_FOUND) {
        return error;
    }
    if (error == ESP_OK) {
        error = nvs_commit(config_nvs);
        if (error != ESP_OK) {
            return error;
        }
    }

    amulet_response_config_t default_config;
    get_effective_default(target, &default_config);
    xSemaphoreTake(config_mutex, portMAX_DELAY);
    current_configs[target] = default_config;
    xSemaphoreGive(config_mutex);
    ESP_LOGI(TAG, "restored default local %s", CONFIG_NAMES[target]);
    return ESP_OK;
}

esp_err_t amulet_config_set_factory_default(
    amulet_config_target_t target,
    const amulet_response_config_t *config
)
{
    if (!target_is_valid(target) || !amulet_config_is_valid(config)) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t error = nvs_set_blob(
        config_nvs,
        FACTORY_NVS_KEYS[target],
        config,
        sizeof(*config)
    );
    if (error != ESP_OK) {
        return error;
    }
    // The new default also becomes the current pattern.
    error = nvs_erase_key(config_nvs, CONFIG_NVS_KEYS[target]);
    if (error != ESP_OK && error != ESP_ERR_NVS_NOT_FOUND) {
        return error;
    }
    error = nvs_commit(config_nvs);
    if (error != ESP_OK) {
        return error;
    }

    xSemaphoreTake(config_mutex, portMAX_DELAY);
    factory_configs[target] = *config;
    factory_config_present[target] = true;
    current_configs[target] = *config;
    xSemaphoreGive(config_mutex);
    ESP_LOGI(TAG, "set factory %s with %u step(s)", CONFIG_NAMES[target],
              (unsigned int)config->step_count);
    return ESP_OK;
}
