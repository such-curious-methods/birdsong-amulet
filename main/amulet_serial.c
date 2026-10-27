#include "amulet_serial.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#if !CONFIG_ESP_CONSOLE_UART && !CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
#error "The Amulet Web Serial protocol requires a primary UART or USB Serial/JTAG console"
#endif
#define AMULET_PROTOCOL_PREFIX "AMULET/2 "
#define AMULET_SERIAL_LINE_LENGTH 192
#define AMULET_SERIAL_POLL_DELAY_MS 10

static const char *TAG = "amulet_serial";
static amulet_response_preview_fn preview_callback;

static void send_response(const char *response)
{
    printf(AMULET_PROTOCOL_PREFIX "%s\n", response);
    fflush(stdout);
}

static void send_config(amulet_config_target_t target)
{
    amulet_response_config_t config;
    amulet_config_get(target, &config);

    char response[AMULET_SERIAL_LINE_LENGTH];
    size_t offset = (size_t)snprintf(
        response,
        sizeof(response),
        "%s %u:%u ",
        target == AMULET_CONFIG_CALL ? "CALL_CONFIG" : "CONFIG",
        (unsigned int)config.note_duration_ms,
        (unsigned int)config.start_delay_ms
    );
    for (uint8_t index = 0; index < config.step_count; index++) {
        const amulet_response_step_t *step = &config.steps[index];
        int written = snprintf(
            response + offset,
            sizeof(response) - offset,
            "%s%u:%02X%02X%02X",
            index == 0 ? "" : ",",
            (unsigned int)step->frequency_hz,
            (unsigned int)step->red,
            (unsigned int)step->green,
            (unsigned int)step->blue
        );
        if (written < 0 || (size_t)written >= sizeof(response) - offset) {
            send_response("ERROR INTERNAL");
            return;
        }
        offset += (size_t)written;
    }
    send_response(response);
}

static bool parse_decimal(
    const char *text,
    unsigned long minimum,
    unsigned long maximum,
    uint16_t *result
)
{
    if (*text == '\0') {
        return false;
    }
    for (const char *character = text; *character != '\0'; character++) {
        if (!isdigit((unsigned char)*character)) {
            return false;
        }
    }

    char *end;
    unsigned long value = strtoul(text, &end, 10);
    if (*end != '\0' ||
        value < minimum ||
        value > maximum) {
        return false;
    }

    *result = (uint16_t)value;
    return true;
}

static bool parse_rgb(const char *text, amulet_response_step_t *step)
{
    if (strlen(text) != 6) {
        return false;
    }
    for (size_t index = 0; index < 6; index++) {
        if (!isxdigit((unsigned char)text[index])) {
            return false;
        }
    }

    char *end;
    unsigned long value = strtoul(text, &end, 16);
    if (*end != '\0') {
        return false;
    }
    step->red = (uint8_t)(value >> 16);
    step->green = (uint8_t)(value >> 8);
    step->blue = (uint8_t)value;
    return true;
}

static bool parse_response(
    amulet_config_target_t target,
    char *text,
    amulet_response_config_t *config
)
{
    amulet_config_set_defaults(target, config);
    memset(config->steps, 0, sizeof(config->steps));
    config->step_count = 0;

    char *steps = strchr(text, ' ');
    if (steps == NULL || steps[1] == '\0') {
        return false;
    }
    *steps = '\0';
    steps++;

    char *delay = strchr(text, ':');
    if (delay == NULL || strchr(delay + 1, ':') != NULL) {
        return false;
    }
    *delay = '\0';
    delay++;
    if (!parse_decimal(
            text,
            AMULET_RESPONSE_MIN_NOTE_DURATION_MS,
            AMULET_RESPONSE_MAX_NOTE_DURATION_MS,
            &config->note_duration_ms
        ) ||
        !parse_decimal(
            delay,
            0,
            AMULET_RESPONSE_MAX_START_DELAY_MS,
            &config->start_delay_ms
        )) {
        return false;
    }

    char *cursor = steps;
    while (*cursor != '\0') {
        if (config->step_count >= AMULET_RESPONSE_MAX_STEPS) {
            return false;
        }

        char *separator = strchr(cursor, ',');
        if (separator != NULL) {
            *separator = '\0';
        }
        char *color = strchr(cursor, ':');
        if (color == NULL || strchr(color + 1, ':') != NULL) {
            return false;
        }
        *color = '\0';
        color++;

        amulet_response_step_t *step = &config->steps[config->step_count];
        /* Rests (0) parse here; amulet_config_is_valid() below rejects the
         * gap between a rest and the lowest playable note. */
        if (!parse_decimal(
                cursor,
                AMULET_RESPONSE_REST_FREQUENCY_HZ,
                AMULET_RESPONSE_MAX_FREQUENCY_HZ,
                &step->frequency_hz
            ) ||
            !parse_rgb(color, step)) {
            return false;
        }
        config->step_count++;

        if (separator == NULL) {
            break;
        }
        cursor = separator + 1;
        if (*cursor == '\0') {
            return false;
        }
    }

    return amulet_config_is_valid(config);
}

static void handle_command(char *line)
{
    if (strncmp(line, AMULET_PROTOCOL_PREFIX, strlen(AMULET_PROTOCOL_PREFIX)) != 0) {
        return;
    }

    char *command = line + strlen(AMULET_PROTOCOL_PREFIX);
    if (strcmp(command, "GET") == 0) {
        send_config(AMULET_CONFIG_RESPONSE);
        return;
    }
    if (strcmp(command, "GET C") == 0) {
        send_config(AMULET_CONFIG_CALL);
        return;
    }
    if (strcmp(command, "PING") == 0) {
        char response[64];
        snprintf(
            response,
            sizeof(response),
            "READY %d %d %d %d %d %d",
            AMULET_RESPONSE_MAX_STEPS,
            AMULET_RESPONSE_MIN_FREQUENCY_HZ,
            AMULET_RESPONSE_MAX_FREQUENCY_HZ,
            AMULET_RESPONSE_MIN_NOTE_DURATION_MS,
            AMULET_RESPONSE_MAX_NOTE_DURATION_MS,
            AMULET_RESPONSE_MAX_START_DELAY_MS
        );
        send_response(response);
        return;
    }
    if (strcmp(command, "RESET") == 0) {
        if (amulet_config_reset(AMULET_CONFIG_RESPONSE) != ESP_OK) {
            send_response("ERROR RESET_FAILED");
            return;
        }
        send_config(AMULET_CONFIG_RESPONSE);
        return;
    }
    if (strcmp(command, "RESET C") == 0) {
        if (amulet_config_reset(AMULET_CONFIG_CALL) != ESP_OK) {
            send_response("ERROR RESET_FAILED");
            return;
        }
        send_config(AMULET_CONFIG_CALL);
        return;
    }

    bool call_save = strncmp(command, "SAVE C ", 7) == 0;
    bool call_preview = strncmp(command, "PREVIEW C ", 10) == 0;
    bool call_default = strncmp(command, "DEFAULT C ", 10) == 0;
    bool save = call_save || strncmp(command, "SAVE ", 5) == 0;
    bool preview = call_preview || strncmp(command, "PREVIEW ", 8) == 0;
    bool set_default = call_default || strncmp(command, "DEFAULT ", 8) == 0;
    if (!save && !preview && !set_default) {
        send_response("ERROR UNKNOWN_COMMAND");
        return;
    }

    amulet_config_target_t target = call_save || call_preview || call_default
        ? AMULET_CONFIG_CALL
        : AMULET_CONFIG_RESPONSE;
    amulet_response_config_t config;
    char *pattern = command + (call_save ? 7
        : call_preview || call_default ? 10
        : save ? 5
        : 8);
    if (!parse_response(target, pattern, &config)) {
        send_response("ERROR INVALID_PATTERN");
        return;
    }

    if (set_default) {
        if (amulet_config_set_factory_default(target, &config) != ESP_OK) {
            send_response("ERROR SAVE_FAILED");
            return;
        }
        send_response(target == AMULET_CONFIG_CALL ? "OK CALL_DEFAULT_SAVED" : "OK DEFAULT_SAVED");
    } else if (save) {
        if (amulet_config_save(target, &config) != ESP_OK) {
            send_response("ERROR SAVE_FAILED");
            return;
        }
        send_response(target == AMULET_CONFIG_CALL ? "OK CALL_SAVED" : "OK SAVED");
    } else if (!preview_callback(target, &config)) {
        send_response("ERROR PREVIEW_BUSY");
    } else {
        send_response(
            target == AMULET_CONFIG_CALL
                ? "OK CALL_PREVIEWING"
                : "OK PREVIEWING"
        );
    }
}

static void serial_task(void *context)
{
    (void)context;
    char line[AMULET_SERIAL_LINE_LENGTH];
    size_t line_length = 0;
    bool line_overflow = false;

    send_response("READY");
    while (true) {
        uint8_t received;
        bool received_any = false;
        while (read(STDIN_FILENO, &received, sizeof(received)) == sizeof(received)) {
            received_any = true;
            char character = (char)received;
            if (character == '\r' || character == '\n') {
                if (line_overflow) {
                    send_response("ERROR LINE_TOO_LONG");
                } else if (line_length > 0) {
                    line[line_length] = '\0';
                    handle_command(line);
                }
                line_length = 0;
                line_overflow = false;
            } else if (!line_overflow) {
                if (line_length < sizeof(line) - 1) {
                    line[line_length++] = character;
                } else {
                    line_overflow = true;
                }
            }
        }
        if (!received_any) {
            vTaskDelay(pdMS_TO_TICKS(AMULET_SERIAL_POLL_DELAY_MS));
        }
    }
}

esp_err_t amulet_serial_init(amulet_response_preview_fn preview_response)
{
    if (preview_response == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    preview_callback = preview_response;

    BaseType_t task_created = xTaskCreate(
        serial_task,
        "amulet_serial",
        3072,
        NULL,
        5,
        NULL
    );
    if (task_created != pdPASS) {
        return ESP_ERR_NO_MEM;
    }

#if CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
    ESP_LOGI(TAG, "Web Serial protocol ready on USB Serial/JTAG");
#else
    ESP_LOGI(TAG, "Web Serial protocol ready at %d baud", CONFIG_ESP_CONSOLE_UART_BAUDRATE);
#endif
    return ESP_OK;
}
