#include <stdbool.h>
#include <stdatomic.h>
#include <stdint.h>
#include <string.h>

#include "amulet_ambient_runtime.h"
#include "amulet_ble_classifier.h"
#include "amulet_config.h"
#include "amulet_hunt.h"
#include "amulet_serial.h"
#include "amulet_signal_gate.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_bt.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "led_strip.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "nvs_flash.h"

#define AMULET_LED_GPIO CONFIG_AMULET_LED_GPIO
#define AMULET_LED_POWER_GPIO CONFIG_AMULET_LED_POWER_GPIO
#define AMULET_BUZZER_GPIO CONFIG_AMULET_BUZZER_GPIO
#define AMULET_BUTTON_GPIO CONFIG_AMULET_BUTTON_GPIO
#define AMULET_EXTERNAL_BUTTON_GPIO CONFIG_AMULET_EXTERNAL_BUTTON_GPIO
#define ALERT_LED_RED 48
#define BROADCAST_LED_BLUE 16
#define PEER_ALERT_LED_MAX 48
#define ALERT_COOLDOWN_US (2000LL * 1000LL)
#define BUZZER_INITIAL_FREQUENCY_HZ 2000
#define TRACKER_ALERT_BUZZER_DUTY 512
#define PEER_ALERT_BUZZER_DUTY 512
#define TRACKER_ALERT_NOTE_DURATION_MS 333
#define BUTTON_DEBOUNCE_MS 40
#define BUTTON_RELEASE_POLL_MS 10
#define PEER_ADVERTISEMENT_DURATION_MS 3000
#define PEER_ADVERTISEMENT_INTERVAL_MS 100
#define SCAN_RETRY_DELAY_MS 250
#define ALERT_QUEUE_LENGTH 16
#define ALERT_QUEUE_POLL_MS 50
#define RECENT_PEER_SESSION_COUNT 16
#define PEER_REPORT_QUEUE_LENGTH 32
#define PEER_RSSI_MAX_SAMPLES 5
#define PEER_RSSI_SAMPLE_WINDOW_MS 450
#define PEER_RSSI_UNKNOWN 127
#define PEER_RSSI_STRONG_DBM -55
#define PEER_RSSI_MODERATE_DBM -65
#define PEER_RSSI_WEAK_DBM -75
#define PEER_RSSI_MODERATE_DELAY_MS 300
#define PEER_RSSI_WEAK_DELAY_MS 700
#define PEER_RSSI_VERY_WEAK_DELAY_MS 1200
#define SIGNAL_REPORT_QUEUE_LENGTH 16
#define SIGNAL_BLINK_ON_MS 150
#define SIGNAL_BLINK_OFF_MS 150
#define SIGNAL_GLASSES_LED_RED 22
#define SIGNAL_GLASSES_LED_GREEN 0
#define SIGNAL_GLASSES_LED_BLUE 36
#define SIGNAL_DRONE_LED_RED 24
#define SIGNAL_DRONE_LED_GREEN 24
#define SIGNAL_DRONE_LED_BLUE 24
#define SIGNAL_POLICE_MAKER_LED_RED 0
#define SIGNAL_POLICE_MAKER_LED_GREEN 8
#define SIGNAL_POLICE_MAKER_LED_BLUE 44
// Two blinks for tracker species and smart glasses, three for surveillance
// gear, so the rhythm says the group and the color says the kind.
#define SIGNAL_SPECIES_BLINKS 2
#define SIGNAL_SURVEILLANCE_BLINKS 3
#define BREATHE_UPDATE_MS 20
#define HUNT_RAINBOW_LEVEL 40
#define HUNT_RAINBOW_SWEEP_MS 2000
#define HUNT_RAINBOW_DURATION_MS 10000
#define HUNT_RAINBOW_STEP_MS 20
#define HUNT_REPORT_QUEUE_LENGTH 16
#define HUNT_REARM_US ((int64_t)CONFIG_AMULET_HUNT_REARM_MINUTES * 60LL * 1000LL * 1000LL)
#define HUNT_BEACON_ADVERTISEMENT_INTERVAL_MS 250
#define HUNT_BEACON_GLOW_LEVEL 40
#define HUNT_BEACON_GLOW_SWEEP_MS 6000

_Static_assert(
    AMULET_HUNT_MAX_BEACONS == AMULET_HUNT_BEACON_MAX_NUMBER,
    "hunt state and beacon frame must agree on the beacon count"
);
#define BREATHE_LEVEL_MAX 1024

#define BUZZER_LEDC_MODE LEDC_LOW_SPEED_MODE
#define BUZZER_LEDC_TIMER LEDC_TIMER_0
#define BUZZER_LEDC_CHANNEL LEDC_CHANNEL_0

static const char *TAG = "amulet";
static const uint32_t TRACKER_ALERT_NOTES_HZ[] = {880, 659, 523, 440};
static const ble_uuid128_t PEER_SERVICE_UUID = BLE_UUID128_INIT(
    AMULET_PEER_SERVICE_UUID_BYTES
);

static led_strip_handle_t led_strip;
static SemaphoreHandle_t led_mutex;
static QueueHandle_t tracker_alert_queue;
static QueueHandle_t call_alert_queue;
static QueueHandle_t response_alert_queue;
static QueueHandle_t peer_report_queue;
static TaskHandle_t button_task_handle;
static TaskHandle_t advertising_task_handle;
static TaskHandle_t scan_retry_task_handle;
static int64_t last_alert_time_us = -ALERT_COOLDOWN_US;
static ble_addr_t recent_peer_addresses[RECENT_PEER_SESSION_COUNT];
static size_t recent_peer_address_count;
static size_t next_peer_address_index;
static uint8_t peer_alert_led_red;
static uint8_t peer_alert_led_green;
static uint8_t peer_alert_led_blue;
static atomic_bool ble_ready;
static atomic_bool preview_pending;
static atomic_int outgoing_state;
static atomic_uint peer_report_drop_count;
static atomic_uint peer_report_producer_count;
static atomic_uint_fast32_t peer_response_generation;
static bool tracker_alert_active;
static bool peer_alert_active;
static bool peer_advertising_active;
static bool signal_indicator_active;
static bool hunt_rainbow_active;
static uint8_t hunt_rainbow_led_red;
static uint8_t hunt_rainbow_led_green;
static uint8_t hunt_rainbow_led_blue;
static QueueHandle_t hunt_alert_queue;
static QueueHandle_t hunt_report_queue;
static atomic_uint hunt_celebrated_beacons;
static uint8_t signal_indicator_led_red;
static uint8_t signal_indicator_led_green;
static uint8_t signal_indicator_led_blue;
#ifdef CONFIG_AMULET_IDLE_BREATHE
static uint16_t breathe_level;
#endif
#ifdef CONFIG_AMULET_PROVISIONAL_SIGNATURES
static QueueHandle_t signal_report_queue;
static atomic_uint signal_report_drop_count;
#endif

typedef enum {
    ALERT_EVENT_TRACKER,
    ALERT_EVENT_CALL,
    ALERT_EVENT_PEER,
    ALERT_EVENT_PREVIEW,
} alert_event_type_t;

typedef enum {
    OUTGOING_IDLE,
    OUTGOING_CALL,
    OUTGOING_STARTING,
    OUTGOING_ADVERTISING,
    OUTGOING_FINISHING,
} outgoing_state_t;

typedef struct {
    alert_event_type_t type;
    amulet_response_config_t response;
    uint32_t peer_generation;
} alert_event_t;

typedef struct {
    ble_addr_t address;
    int64_t received_us;
    uint32_t generation;
    int8_t rssi;
    bool rssi_valid;
} peer_report_t;

typedef struct {
    ble_addr_t address;
    int64_t deadline_us;
    int8_t rssi_samples[PEER_RSSI_MAX_SAMPLES];
    uint8_t sample_count;
    uint16_t automatic_delay_ms;
    uint32_t generation;
    bool active;
    bool sampling;
} pending_peer_session_t;

static pending_peer_session_t pending_peer_sessions[RECENT_PEER_SESSION_COUNT];

// Provisional signature signals from one advertisement; no identity or address.
typedef struct {
    int64_t seen_us;
    uint8_t signals;
    uint8_t tracker_families;
} signal_report_t;

// One hunt-beacon sighting: the beacon's number and signal, never its address.
typedef struct {
    int64_t seen_us;
    uint8_t beacon;
    int8_t rssi;
} hunt_report_t;

void ble_store_config_init(void);
static void request_peer_call(void);
static bool start_peer_advertising(TickType_t *started_at);

static void IRAM_ATTR button_isr(void *context)
{
    (void)context;
    BaseType_t task_woken = pdFALSE;
    vTaskNotifyGiveFromISR(button_task_handle, &task_woken);
    if (task_woken == pdTRUE) {
        portYIELD_FROM_ISR();
    }
}

static bool button_is_pressed(void)
{
    if (gpio_get_level(AMULET_BUTTON_GPIO) == 0) {
        return true;
    }

#if CONFIG_AMULET_EXTERNAL_BUTTON_GPIO >= 0
    return gpio_get_level(AMULET_EXTERNAL_BUTTON_GPIO) == 0;
#else
    return false;
#endif
}

static void button_task(void *context)
{
    (void)context;

    while (true) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        vTaskDelay(pdMS_TO_TICKS(BUTTON_DEBOUNCE_MS));

        if (!button_is_pressed()) {
            continue;
        }

        ESP_LOGI(TAG, "button pressed");
        request_peer_call();
        while (button_is_pressed()) {
            vTaskDelay(pdMS_TO_TICKS(BUTTON_RELEASE_POLL_MS));
        }

        vTaskDelay(pdMS_TO_TICKS(BUTTON_DEBOUNCE_MS));
        ulTaskNotifyTake(pdTRUE, 0);
    }
}

static void initialize_button(void)
{
    const gpio_config_t button_config = {
        .pin_bit_mask = 1ULL << AMULET_BUTTON_GPIO,
        .mode = GPIO_MODE_INPUT,
#if CONFIG_AMULET_BUTTON_INTERNAL_PULLUP
        .pull_up_en = GPIO_PULLUP_ENABLE,
#else
        .pull_up_en = GPIO_PULLUP_DISABLE,
#endif
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    ESP_ERROR_CHECK(gpio_config(&button_config));

#if CONFIG_AMULET_EXTERNAL_BUTTON_GPIO >= 0
    const gpio_config_t external_button_config = {
        .pin_bit_mask = 1ULL << AMULET_EXTERNAL_BUTTON_GPIO,
        .mode = GPIO_MODE_INPUT,
#if CONFIG_AMULET_EXTERNAL_BUTTON_INTERNAL_PULLUP
        .pull_up_en = GPIO_PULLUP_ENABLE,
#else
        .pull_up_en = GPIO_PULLUP_DISABLE,
#endif
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    ESP_ERROR_CHECK(gpio_config(&external_button_config));
#endif

    BaseType_t task_created = xTaskCreate(
        button_task,
        "amulet_button",
        2048,
        NULL,
        5,
        &button_task_handle
    );
    ESP_ERROR_CHECK(task_created == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);

    ESP_ERROR_CHECK(gpio_install_isr_service(0));
    ESP_ERROR_CHECK(gpio_isr_handler_add(AMULET_BUTTON_GPIO, button_isr, NULL));
    ESP_ERROR_CHECK(gpio_set_intr_type(AMULET_BUTTON_GPIO, GPIO_INTR_NEGEDGE));
    ESP_ERROR_CHECK(gpio_intr_enable(AMULET_BUTTON_GPIO));

#if CONFIG_AMULET_EXTERNAL_BUTTON_GPIO >= 0
    ESP_ERROR_CHECK(gpio_isr_handler_add(
        AMULET_EXTERNAL_BUTTON_GPIO,
        button_isr,
        NULL
    ));
    ESP_ERROR_CHECK(gpio_set_intr_type(
        AMULET_EXTERNAL_BUTTON_GPIO,
        GPIO_INTR_NEGEDGE
    ));
    ESP_ERROR_CHECK(gpio_intr_enable(AMULET_EXTERNAL_BUTTON_GPIO));
#endif
}

static void start_buzzer(uint32_t frequency_hz, uint32_t duty)
{
    ESP_ERROR_CHECK(ledc_set_freq(
        BUZZER_LEDC_MODE,
        BUZZER_LEDC_TIMER,
        frequency_hz
    ));
    ESP_ERROR_CHECK(ledc_set_duty(BUZZER_LEDC_MODE, BUZZER_LEDC_CHANNEL, duty));
    ESP_ERROR_CHECK(ledc_update_duty(BUZZER_LEDC_MODE, BUZZER_LEDC_CHANNEL));
}

static void stop_buzzer(void)
{
    ESP_ERROR_CHECK(ledc_set_duty(BUZZER_LEDC_MODE, BUZZER_LEDC_CHANNEL, 0));
    ESP_ERROR_CHECK(ledc_update_duty(BUZZER_LEDC_MODE, BUZZER_LEDC_CHANNEL));
}

static void play_notes(const uint32_t *notes_hz, size_t note_count, uint32_t duty)
{
    for (size_t index = 0; index < note_count; index++) {
        start_buzzer(notes_hz[index], duty);
        vTaskDelay(pdMS_TO_TICKS(TRACKER_ALERT_NOTE_DURATION_MS));
    }
    stop_buzzer();
}

#ifdef CONFIG_AMULET_IDLE_BREATHE
// With a floor, a lit channel never rounds down to dark, so the frozen LED
// after the Power switch resets the chip always shows a glow.
static uint8_t breathe_component(uint8_t peak)
{
    uint32_t value = (uint32_t)peak * breathe_level / BREATHE_LEVEL_MAX;
    if (CONFIG_AMULET_IDLE_BREATHE_FLOOR_PERCENT > 0 && peak > 0 && value == 0) {
        value = 1;
    }
    return (uint8_t)value;
}
#endif

static void refresh_led(void)
{
    if (tracker_alert_active) {
        ESP_ERROR_CHECK(led_strip_set_pixel(led_strip, 0, ALERT_LED_RED, 0, 0));
        ESP_ERROR_CHECK(led_strip_refresh(led_strip));
    } else if (peer_alert_active) {
        ESP_ERROR_CHECK(led_strip_set_pixel(
            led_strip,
            0,
            peer_alert_led_red,
            peer_alert_led_green,
            peer_alert_led_blue
        ));
        ESP_ERROR_CHECK(led_strip_refresh(led_strip));
    } else if (peer_advertising_active) {
        ESP_ERROR_CHECK(led_strip_set_pixel(led_strip, 0, 0, 0, BROADCAST_LED_BLUE));
        ESP_ERROR_CHECK(led_strip_refresh(led_strip));
    } else if (hunt_rainbow_active) {
        ESP_ERROR_CHECK(led_strip_set_pixel(
            led_strip,
            0,
            hunt_rainbow_led_red,
            hunt_rainbow_led_green,
            hunt_rainbow_led_blue
        ));
        ESP_ERROR_CHECK(led_strip_refresh(led_strip));
    } else if (signal_indicator_active) {
        ESP_ERROR_CHECK(led_strip_set_pixel(
            led_strip,
            0,
            signal_indicator_led_red,
            signal_indicator_led_green,
            signal_indicator_led_blue
        ));
        ESP_ERROR_CHECK(led_strip_refresh(led_strip));
    } else {
#ifdef CONFIG_AMULET_IDLE_BREATHE
        ESP_ERROR_CHECK(led_strip_set_pixel(
            led_strip,
            0,
            breathe_component(CONFIG_AMULET_IDLE_BREATHE_RED),
            breathe_component(CONFIG_AMULET_IDLE_BREATHE_GREEN),
            breathe_component(CONFIG_AMULET_IDLE_BREATHE_BLUE)
        ));
        ESP_ERROR_CHECK(led_strip_refresh(led_strip));
#else
        ESP_ERROR_CHECK(led_strip_clear(led_strip));
#endif
    }
}

#ifdef CONFIG_AMULET_IDLE_BREATHE
static bool led_idle(void)
{
    return !tracker_alert_active
        && !peer_alert_active
        && !peer_advertising_active
        && !hunt_rainbow_active
        && !signal_indicator_active;
}

// Squaring a triangle wave approximates the eye's gamma curve, so the fade
// lingers near its floor the way a laptop sleep light does. Integer-only
// because the ESP32-C3 has no FPU.
static uint16_t breathe_level_at(int64_t now_us)
{
    const uint32_t period_ms = CONFIG_AMULET_IDLE_BREATHE_PERIOD_MS;
    const uint32_t floor_level = BREATHE_LEVEL_MAX * CONFIG_AMULET_IDLE_BREATHE_FLOOR_PERCENT / 100;
    uint32_t phase_ms = (uint32_t)((now_us / 1000) % period_ms);
    uint32_t half_ms = period_ms / 2;
    uint32_t ramp = phase_ms < half_ms ? phase_ms : period_ms - phase_ms;
    uint32_t linear = ramp * BREATHE_LEVEL_MAX / half_ms;
    uint32_t eased = linear * linear / BREATHE_LEVEL_MAX;
    return (uint16_t)(floor_level + (BREATHE_LEVEL_MAX - floor_level) * eased / BREATHE_LEVEL_MAX);
}

// The lowest LED priority, below even the signal indicator. Skips the RMT
// write when the visible color would not change.
static void breathe_task(void *arg)
{
    (void)arg;
    TickType_t last_wake = xTaskGetTickCount();
    for (;;) {
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(BREATHE_UPDATE_MS));
        uint16_t level = breathe_level_at(esp_timer_get_time());
        xSemaphoreTake(led_mutex, portMAX_DELAY);
        if (level != breathe_level) {
            breathe_level = level;
            if (led_idle()) {
                refresh_led();
            }
        }
        xSemaphoreGive(led_mutex);
    }
}
#endif

static void set_tracker_alert_active(bool active)
{
    xSemaphoreTake(led_mutex, portMAX_DELAY);
    tracker_alert_active = active;
    refresh_led();
    xSemaphoreGive(led_mutex);
}

static void set_peer_alert_active(bool active)
{
    xSemaphoreTake(led_mutex, portMAX_DELAY);
    peer_alert_active = active;
    refresh_led();
    xSemaphoreGive(led_mutex);
}

static uint8_t scale_peer_led_component(uint8_t component)
{
    return (uint8_t)(((uint16_t)component * PEER_ALERT_LED_MAX + 127) / 255);
}

static void set_peer_alert_step(const amulet_response_step_t *step)
{
    xSemaphoreTake(led_mutex, portMAX_DELAY);
    peer_alert_led_red = scale_peer_led_component(step->red);
    peer_alert_led_green = scale_peer_led_component(step->green);
    peer_alert_led_blue = scale_peer_led_component(step->blue);
    peer_alert_active = true;
    refresh_led();
    xSemaphoreGive(led_mutex);
}

// Below calls, responses and broadcasting; above signal blinks and breathing.
static void set_hunt_rainbow(bool active, uint8_t red, uint8_t green, uint8_t blue)
{
    xSemaphoreTake(led_mutex, portMAX_DELAY);
    hunt_rainbow_led_red = red;
    hunt_rainbow_led_green = green;
    hunt_rainbow_led_blue = blue;
    hunt_rainbow_active = active;
    refresh_led();
    xSemaphoreGive(led_mutex);
}

static void set_peer_advertising_active(bool active)
{
    xSemaphoreTake(led_mutex, portMAX_DELAY);
    peer_advertising_active = active;
    refresh_led();
    xSemaphoreGive(led_mutex);
}

#ifdef CONFIG_AMULET_PROVISIONAL_SIGNATURES
// The lowest LED priority: alerts, melodies and broadcasting all hide it.
static void set_signal_indicator(bool active, uint8_t red, uint8_t green, uint8_t blue)
{
    xSemaphoreTake(led_mutex, portMAX_DELAY);
    signal_indicator_led_red = red;
    signal_indicator_led_green = green;
    signal_indicator_led_blue = blue;
    signal_indicator_active = active;
    refresh_led();
    xSemaphoreGive(led_mutex);
}
#endif

static void play_tracker_alert(void)
{
    set_tracker_alert_active(true);
    play_notes(
        TRACKER_ALERT_NOTES_HZ,
        sizeof(TRACKER_ALERT_NOTES_HZ) / sizeof(TRACKER_ALERT_NOTES_HZ[0]),
        TRACKER_ALERT_BUZZER_DUTY
    );
    set_tracker_alert_active(false);
}

static void play_pending_tracker_alerts(void)
{
    alert_event_t event;
    while (xQueueReceive(tracker_alert_queue, &event, 0) == pdTRUE) {
        play_tracker_alert();
    }
}

static void discard_pending_responses(void)
{
    alert_event_t event;
    while (xQueueReceive(response_alert_queue, &event, 0) == pdTRUE) {
        if (event.type == ALERT_EVENT_PREVIEW) {
            atomic_store(&preview_pending, false);
        }
    }
}

static void clear_outgoing_state(outgoing_state_t expected_state)
{
    int expected = expected_state;
    atomic_compare_exchange_strong(
        &outgoing_state,
        &expected,
        OUTGOING_IDLE
    );
}

static void stop_configured_pattern(void)
{
    stop_buzzer();
    set_peer_alert_active(false);
}

static bool configured_pattern_should_stop(
    const alert_event_t *event,
    bool yield_to_call
)
{
    return (yield_to_call && atomic_load(&outgoing_state) != OUTGOING_IDLE) ||
           (event->type == ALERT_EVENT_PEER &&
            event->peer_generation !=
                (uint32_t)atomic_load(&peer_response_generation));
}

static void play_configured_pattern(const alert_event_t *event, bool yield_to_call)
{
    const amulet_response_config_t *response = &event->response;
    play_pending_tracker_alerts();
    if (configured_pattern_should_stop(event, yield_to_call)) {
        stop_configured_pattern();
        return;
    }
    if (response->start_delay_ms > 0) {
        vTaskDelay(pdMS_TO_TICKS(response->start_delay_ms));
        play_pending_tracker_alerts();
        if (configured_pattern_should_stop(event, yield_to_call)) {
            stop_configured_pattern();
            return;
        }
    }
    for (uint8_t index = 0; index < response->step_count; index++) {
        const amulet_response_step_t *step = &response->steps[index];
        set_peer_alert_step(step);
        // A rest keeps its light but silences the buzzer; ledc_set_freq()
        // cannot take 0 Hz. The next note's start_buzzer() restores the duty.
        if (step->frequency_hz == AMULET_RESPONSE_REST_FREQUENCY_HZ) {
            stop_buzzer();
        } else {
            start_buzzer(step->frequency_hz, PEER_ALERT_BUZZER_DUTY);
        }
        vTaskDelay(pdMS_TO_TICKS(response->note_duration_ms));
        play_pending_tracker_alerts();
        if (configured_pattern_should_stop(event, yield_to_call)) {
            stop_configured_pattern();
            return;
        }
    }
    stop_configured_pattern();
}

static void play_response_event(const alert_event_t *event)
{
    play_configured_pattern(event, true);
    if (event->type == ALERT_EVENT_PREVIEW) {
        atomic_store(&preview_pending, false);
    }
}

// The rainbow never interrupts anything, and anything else that wants the LED
// or buzzer interrupts it: a Call keeps its timing, a Response keeps its place
// in a staggered chord.
static bool hunt_rainbow_should_yield(void)
{
    return uxQueueMessagesWaiting(tracker_alert_queue) > 0 ||
           uxQueueMessagesWaiting(call_alert_queue) > 0 ||
           uxQueueMessagesWaiting(response_alert_queue) > 0 ||
           atomic_load(&outgoing_state) != OUTGOING_IDLE;
}

// Five silent hue sweeps. Returns false when interrupted, so the caller can
// replay the rainbow from the start once the Amulet is idle again.
static bool play_hunt_rainbow(void)
{
    TickType_t last_wake = xTaskGetTickCount();
    for (uint32_t elapsed_ms = 0;
         elapsed_ms < HUNT_RAINBOW_DURATION_MS;
         elapsed_ms += HUNT_RAINBOW_STEP_MS) {
        if (hunt_rainbow_should_yield()) {
            set_hunt_rainbow(false, 0, 0, 0);
            return false;
        }
        uint8_t red;
        uint8_t green;
        uint8_t blue;
        amulet_hunt_rainbow_rgb(
            (elapsed_ms % HUNT_RAINBOW_SWEEP_MS) * AMULET_HUNT_HUE_STEPS /
                HUNT_RAINBOW_SWEEP_MS,
            HUNT_RAINBOW_LEVEL,
            &red,
            &green,
            &blue
        );
        set_hunt_rainbow(true, red, green, blue);
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(HUNT_RAINBOW_STEP_MS));
    }
    set_hunt_rainbow(false, 0, 0, 0);
    return true;
}

static void alert_task(void *context)
{
    (void)context;
    uint8_t rainbow_beacon = 0;

    while (true) {
        alert_event_t event;
        if (xQueueReceive(tracker_alert_queue, &event, 0) == pdTRUE) {
            play_tracker_alert();
            continue;
        }
        if (xQueueReceive(call_alert_queue, &event, 0) == pdTRUE) {
            discard_pending_responses();
            play_configured_pattern(&event, false);
            discard_pending_responses();
            xTaskNotifyGive(advertising_task_handle);
            continue;
        }
        if (atomic_load(&outgoing_state) != OUTGOING_IDLE) {
            vTaskDelay(pdMS_TO_TICKS(ALERT_QUEUE_POLL_MS));
            continue;
        }
        if (xQueueReceive(response_alert_queue, &event, 0) == pdTRUE) {
            play_response_event(&event);
            continue;
        }
        // An interrupted rainbow stays in rainbow_beacon and replays here.
        if (rainbow_beacon == 0) {
            xQueueReceive(hunt_alert_queue, &rainbow_beacon, 0);
        }
        if (rainbow_beacon != 0) {
            if (play_hunt_rainbow()) {
                atomic_fetch_or(&hunt_celebrated_beacons, 1U << (rainbow_beacon - 1U));
                rainbow_beacon = 0;
            }
            continue;
        }
        if (xQueueReceive(
                response_alert_queue,
                &event,
                pdMS_TO_TICKS(ALERT_QUEUE_POLL_MS)
            ) == pdTRUE) {
            play_response_event(&event);
        }
    }
}

static void initialize_buzzer(void)
{
    const ledc_timer_config_t timer_config = {
        .speed_mode = BUZZER_LEDC_MODE,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .timer_num = BUZZER_LEDC_TIMER,
        .freq_hz = BUZZER_INITIAL_FREQUENCY_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    const ledc_channel_config_t channel_config = {
        .gpio_num = AMULET_BUZZER_GPIO,
        .speed_mode = BUZZER_LEDC_MODE,
        .channel = BUZZER_LEDC_CHANNEL,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = BUZZER_LEDC_TIMER,
        .duty = 0,
        .hpoint = 0,
    };

    ESP_ERROR_CHECK(ledc_timer_config(&timer_config));
    ESP_ERROR_CHECK(ledc_channel_config(&channel_config));
}

static void initialize_led_strip(void)
{
#if AMULET_LED_POWER_GPIO >= 0
    const gpio_config_t led_power_config = {
        .pin_bit_mask = 1ULL << AMULET_LED_POWER_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&led_power_config));
    ESP_ERROR_CHECK(gpio_set_level(AMULET_LED_POWER_GPIO, 1));
#endif

    led_mutex = xSemaphoreCreateMutex();
    ESP_ERROR_CHECK(led_mutex != NULL ? ESP_OK : ESP_ERR_NO_MEM);

    const led_strip_config_t strip_config = {
        .strip_gpio_num = AMULET_LED_GPIO,
        .max_leds = 1,
        .led_model = LED_MODEL_WS2812,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
        .flags.invert_out = false,
    };
    const led_strip_rmt_config_t rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000,
        .mem_block_symbols = 0,
        .flags.with_dma = false,
    };

    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip));
    ESP_ERROR_CHECK(led_strip_clear(led_strip));
}

static void initialize_led(void)
{
    initialize_led_strip();

    tracker_alert_queue = xQueueCreate(1, sizeof(alert_event_t));
    call_alert_queue = xQueueCreate(1, sizeof(alert_event_t));
    response_alert_queue = xQueueCreate(ALERT_QUEUE_LENGTH, sizeof(alert_event_t));
    hunt_alert_queue = xQueueCreate(AMULET_HUNT_MAX_BEACONS, sizeof(uint8_t));
    ESP_ERROR_CHECK(tracker_alert_queue != NULL ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(call_alert_queue != NULL ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(response_alert_queue != NULL ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(hunt_alert_queue != NULL ? ESP_OK : ESP_ERR_NO_MEM);

    // A brief blue pulse confirms the addressable LED works after each boot.
    ESP_ERROR_CHECK(led_strip_set_pixel(led_strip, 0, 0, 0, BROADCAST_LED_BLUE));
    ESP_ERROR_CHECK(led_strip_refresh(led_strip));
    vTaskDelay(pdMS_TO_TICKS(100));
    ESP_ERROR_CHECK(led_strip_clear(led_strip));

    BaseType_t task_created = xTaskCreate(
        alert_task,
        "amulet_alert",
        4096,
        NULL,
        5,
        NULL
    );
    ESP_ERROR_CHECK(task_created == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);

#ifdef CONFIG_AMULET_IDLE_BREATHE
    task_created = xTaskCreate(
        breathe_task,
        "amulet_breathe",
        2048,
        NULL,
        1,
        NULL
    );
    ESP_ERROR_CHECK(task_created == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
#endif
}

static bool queue_alert(
    alert_event_type_t type,
    const amulet_response_config_t *response
)
{
    alert_event_t event = {.type = type};
    if (response != NULL) {
        event.response = *response;
    }
    QueueHandle_t queue = type == ALERT_EVENT_TRACKER
        ? tracker_alert_queue
        : type == ALERT_EVENT_CALL
            ? call_alert_queue
        : response_alert_queue;
    if (xQueueSend(queue, &event, 0) != pdTRUE) {
        ESP_LOGW(TAG, "alert queue full; dropping event=%d", (int)type);
        return false;
    }
    return true;
}

static bool queue_peer_alert(
    const amulet_response_config_t *response,
    uint32_t generation
)
{
    alert_event_t event = {
        .type = ALERT_EVENT_PEER,
        .response = *response,
        .peer_generation = generation,
    };
    if (xQueueSend(response_alert_queue, &event, 0) != pdTRUE) {
        ESP_LOGW(TAG, "alert queue full; dropping peer response");
        return false;
    }
    return true;
}

static bool queue_response_preview(
    amulet_config_target_t target,
    const amulet_response_config_t *response
)
{
    (void)target;
    if (atomic_load(&outgoing_state) != OUTGOING_IDLE) {
        return false;
    }
    if (atomic_exchange(&preview_pending, true)) {
        return false;
    }
    if (!queue_alert(ALERT_EVENT_PREVIEW, response)) {
        atomic_store(&preview_pending, false);
        return false;
    }
    return true;
}

static void request_peer_call(void)
{
    if (!atomic_load(&ble_ready) || !ble_gap_disc_active()) {
        ESP_LOGW(TAG, "BLE scan is not ready; ignoring call request");
        return;
    }

    int expected = OUTGOING_IDLE;
    if (!atomic_compare_exchange_strong(
            &outgoing_state,
            &expected,
            OUTGOING_CALL
        )) {
        ESP_LOGI(TAG, "call or peer advertising already active; ignoring button press");
        return;
    }

    atomic_fetch_add(&peer_response_generation, 1);

    amulet_response_config_t call;
    amulet_config_get(AMULET_CONFIG_CALL, &call);
    if (!queue_alert(ALERT_EVENT_CALL, &call)) {
        atomic_store(&outgoing_state, OUTGOING_IDLE);
        return;
    }
    ESP_LOGI(TAG, "local call queued before peer advertising");
}

static void handle_test_tracker(const struct ble_gap_disc_desc *advertisement)
{
    int64_t now_us = esp_timer_get_time();
    if (now_us - last_alert_time_us < ALERT_COOLDOWN_US) {
        return;
    }
    if (!queue_alert(ALERT_EVENT_TRACKER, NULL)) {
        return;
    }
    last_alert_time_us = now_us;

    const uint8_t *address = advertisement->addr.val;
    ESP_LOGW(
        TAG,
        "test tracker detected: address=%02x:%02x:%02x:%02x:%02x:%02x type=%u rssi=%d dBm",
        address[5], address[4], address[3], address[2], address[1], address[0],
        (unsigned int)advertisement->addr.type,
        (int)advertisement->rssi
    );
}

static bool peer_addresses_equal(const ble_addr_t *left, const ble_addr_t *right)
{
    return left->type == right->type &&
           memcmp(left->val, right->val, sizeof(left->val)) == 0;
}

static bool is_recent_peer_session(const ble_addr_t *address)
{
    for (size_t index = 0; index < recent_peer_address_count; index++) {
        if (peer_addresses_equal(&recent_peer_addresses[index], address)) {
            return true;
        }
    }

    return false;
}

static void remember_peer_session(const ble_addr_t *address)
{
    recent_peer_addresses[next_peer_address_index] = *address;
    next_peer_address_index =
        (next_peer_address_index + 1) % RECENT_PEER_SESSION_COUNT;
    if (recent_peer_address_count < RECENT_PEER_SESSION_COUNT) {
        recent_peer_address_count++;
    }
}

static int8_t median_peer_rssi(const pending_peer_session_t *session)
{
    int8_t sorted[PEER_RSSI_MAX_SAMPLES];
    memcpy(sorted, session->rssi_samples, session->sample_count);
    for (uint8_t index = 1; index < session->sample_count; index++) {
        int8_t sample = sorted[index];
        uint8_t position = index;
        while (position > 0 && sorted[position - 1] > sample) {
            sorted[position] = sorted[position - 1];
            position--;
        }
        sorted[position] = sample;
    }

    uint8_t middle = session->sample_count / 2;
    if (session->sample_count % 2 != 0) {
        return sorted[middle];
    }
    int16_t sum = (int16_t)sorted[middle - 1] + sorted[middle];
    if (sum < 0 && sum % 2 != 0) {
        sum--;
    }
    return (int8_t)(sum / 2);
}

static uint16_t automatic_response_delay_ms(int8_t median_rssi)
{
    if (median_rssi >= PEER_RSSI_STRONG_DBM) {
        return 0;
    }
    if (median_rssi >= PEER_RSSI_MODERATE_DBM) {
        return PEER_RSSI_MODERATE_DELAY_MS;
    }
    if (median_rssi >= PEER_RSSI_WEAK_DBM) {
        return PEER_RSSI_WEAK_DELAY_MS;
    }
    return PEER_RSSI_VERY_WEAK_DELAY_MS;
}

static pending_peer_session_t *find_pending_peer_session(const ble_addr_t *address)
{
    for (size_t index = 0; index < RECENT_PEER_SESSION_COUNT; index++) {
        pending_peer_session_t *session = &pending_peer_sessions[index];
        if (session->active && peer_addresses_equal(&session->address, address)) {
            return session;
        }
    }
    return NULL;
}

static pending_peer_session_t *allocate_pending_peer_session(void)
{
    for (size_t index = 0; index < RECENT_PEER_SESSION_COUNT; index++) {
        if (!pending_peer_sessions[index].active) {
            return &pending_peer_sessions[index];
        }
    }
    return NULL;
}

static void finalize_peer_sampling(pending_peer_session_t *session)
{
    int8_t median_rssi = session->sample_count > 0
        ? median_peer_rssi(session)
        : 0;
    session->automatic_delay_ms = session->sample_count > 0
        ? automatic_response_delay_ms(median_rssi)
        : 0;
    session->sampling = false;
    session->deadline_us = esp_timer_get_time() +
        (int64_t)session->automatic_delay_ms * 1000LL;

    const uint8_t *address = session->address.val;
    if (session->sample_count > 0) {
        ESP_LOGI(
            TAG,
            "peer Amulet detected: address=%02x:%02x:%02x:%02x:%02x:%02x "
            "type=%u samples=%u median_rssi=%d dBm automatic_delay=%u ms",
            address[5], address[4], address[3], address[2], address[1], address[0],
            (unsigned int)session->address.type,
            (unsigned int)session->sample_count,
            (int)median_rssi,
            (unsigned int)session->automatic_delay_ms
        );
    } else {
        ESP_LOGI(
            TAG,
            "peer Amulet detected: address=%02x:%02x:%02x:%02x:%02x:%02x "
            "type=%u samples=0 median_rssi=unavailable automatic_delay=0 ms",
            address[5], address[4], address[3], address[2], address[1], address[0],
            (unsigned int)session->address.type
        );
    }
}

static void queue_scheduled_peer_response(pending_peer_session_t *session)
{
    if (atomic_load(&outgoing_state) != OUTGOING_IDLE ||
        session->generation != (uint32_t)atomic_load(&peer_response_generation)) {
        session->active = false;
        return;
    }

    amulet_response_config_t response;
    amulet_config_get(AMULET_CONFIG_RESPONSE, &response);
    bool queued = queue_peer_alert(&response, session->generation);
    if (queued) {
        remember_peer_session(&session->address);
        ESP_LOGI(
            TAG,
            "peer response queued after %u ms automatic delay",
            (unsigned int)session->automatic_delay_ms
        );
    }
    session->active = false;
}

static void process_due_peer_sessions(int64_t now_us)
{
    uint32_t generation = (uint32_t)atomic_load(&peer_response_generation);
    if (atomic_load(&outgoing_state) != OUTGOING_IDLE) {
        memset(pending_peer_sessions, 0, sizeof(pending_peer_sessions));
        return;
    }

    for (size_t index = 0; index < RECENT_PEER_SESSION_COUNT; index++) {
        pending_peer_session_t *session = &pending_peer_sessions[index];
        if (!session->active) {
            continue;
        }
        if (session->generation != generation) {
            session->active = false;
            continue;
        }
        if (session->sampling &&
            (session->sample_count >= PEER_RSSI_MAX_SAMPLES ||
             now_us >= session->deadline_us)) {
            finalize_peer_sampling(session);
        }
        if (!session->sampling && esp_timer_get_time() >= session->deadline_us) {
            queue_scheduled_peer_response(session);
        }
    }
}

static TickType_t next_peer_session_timeout(int64_t now_us)
{
    int64_t earliest_deadline_us = 0;
    for (size_t index = 0; index < RECENT_PEER_SESSION_COUNT; index++) {
        const pending_peer_session_t *session = &pending_peer_sessions[index];
        if (session->active &&
            (earliest_deadline_us == 0 || session->deadline_us < earliest_deadline_us)) {
            earliest_deadline_us = session->deadline_us;
        }
    }
    if (earliest_deadline_us == 0) {
        return portMAX_DELAY;
    }
    if (earliest_deadline_us <= now_us) {
        return 0;
    }

    int64_t tick_us = (int64_t)portTICK_PERIOD_MS * 1000LL;
    return (TickType_t)(
        (earliest_deadline_us - now_us + tick_us - 1) / tick_us
    );
}

static void process_peer_report(const peer_report_t *report)
{
    uint32_t generation = (uint32_t)atomic_load(&peer_response_generation);
    if (atomic_load(&outgoing_state) != OUTGOING_IDLE ||
        report->generation != generation ||
        is_recent_peer_session(&report->address)) {
        return;
    }

    pending_peer_session_t *session = find_pending_peer_session(&report->address);
    if (session == NULL) {
        session = allocate_pending_peer_session();
        if (session == NULL) {
            ESP_LOGW(TAG, "peer session table full; dropping RSSI report");
            return;
        }
        *session = (pending_peer_session_t) {
            .address = report->address,
            .deadline_us = report->received_us +
                PEER_RSSI_SAMPLE_WINDOW_MS * 1000LL,
            .generation = report->generation,
            .active = true,
            .sampling = true,
        };
    }
    if (session->generation == report->generation &&
        session->sampling &&
        report->rssi_valid &&
        report->received_us < session->deadline_us &&
        session->sample_count < PEER_RSSI_MAX_SAMPLES) {
        session->rssi_samples[session->sample_count++] = report->rssi;
    }
}

static void peer_response_task(void *context)
{
    (void)context;

    while (true) {
        peer_report_t report;
        while (xQueueReceive(peer_report_queue, &report, 0) == pdTRUE) {
            process_peer_report(&report);
        }

        // Capture time before confirming that no callback with an older
        // timestamp is still preparing or has queued a report.
        int64_t now_us = esp_timer_get_time();
        if (atomic_load(&peer_report_producer_count) > 0 ||
            uxQueueMessagesWaiting(peer_report_queue) > 0) {
            continue;
        }
        process_due_peer_sessions(now_us);

        unsigned int dropped_reports = atomic_exchange(&peer_report_drop_count, 0);
        if (dropped_reports > 0) {
            ESP_LOGW(TAG, "peer RSSI queue dropped %u report(s)", dropped_reports);
        }

        now_us = esp_timer_get_time();
        if (xQueueReceive(
                peer_report_queue,
                &report,
                next_peer_session_timeout(now_us)
            ) == pdTRUE) {
            process_peer_report(&report);
        }
    }
}

static void initialize_peer_response_scheduler(void)
{
    peer_report_queue = xQueueCreate(
        PEER_REPORT_QUEUE_LENGTH,
        sizeof(peer_report_t)
    );
    ESP_ERROR_CHECK(peer_report_queue != NULL ? ESP_OK : ESP_ERR_NO_MEM);

    BaseType_t task_created = xTaskCreate(
        peer_response_task,
        "peer_delay",
        3072,
        NULL,
        5,
        NULL
    );
    ESP_ERROR_CHECK(task_created == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
}

static void handle_peer_amulet(const struct ble_gap_disc_desc *advertisement)
{
    atomic_fetch_add(&peer_report_producer_count, 1);
    uint32_t generation = (uint32_t)atomic_load(&peer_response_generation);
    int64_t received_us = esp_timer_get_time();
    if (atomic_load(&outgoing_state) != OUTGOING_IDLE) {
        atomic_fetch_sub(&peer_report_producer_count, 1);
        return;
    }

    peer_report_t report = {
        .address = advertisement->addr,
        .received_us = received_us,
        .generation = generation,
        .rssi = advertisement->rssi,
        .rssi_valid = advertisement->rssi != PEER_RSSI_UNKNOWN,
    };
    BaseType_t queued = xQueueSend(peer_report_queue, &report, 0);
    atomic_fetch_sub(&peer_report_producer_count, 1);
    if (queued != pdTRUE) {
        atomic_fetch_add(&peer_report_drop_count, 1);
    }
}

static int start_scan(void);
static void ensure_scan_started(void);

#ifdef CONFIG_AMULET_PROVISIONAL_SIGNATURES
static void queue_signal_report(const amulet_ble_advertisement_t *advertisement)
{
    const signal_report_t report = {
        .seen_us = esp_timer_get_time(),
        .signals = advertisement->signals,
        .tracker_families = advertisement->tracker_families,
    };
    if (xQueueSend(signal_report_queue, &report, 0) != pdTRUE) {
        atomic_fetch_add(&signal_report_drop_count, 1);
    }
}

// The gaps stay dark rather than falling back to the idle breathe, so the
// blinks remain countable.
static void blink_signal_indicator(int count, uint8_t red, uint8_t green, uint8_t blue)
{
    for (int blink = 0; blink < count; blink++) {
        set_signal_indicator(true, red, green, blue);
        vTaskDelay(pdMS_TO_TICKS(SIGNAL_BLINK_ON_MS));
        set_signal_indicator(true, 0, 0, 0);
        vTaskDelay(pdMS_TO_TICKS(SIGNAL_BLINK_OFF_MS));
    }
    set_signal_indicator(false, 0, 0, 0);
}

static void describe_tracker_families(uint8_t families, char *text, size_t size)
{
    static const struct {
        uint8_t family;
        const char *name;
    } names[] = {
        {AMULET_BLE_TRACKER_FAMILY_FIND_MY, "Find My"},
        {AMULET_BLE_TRACKER_FAMILY_SMARTTAG, "SmartTag"},
        {AMULET_BLE_TRACKER_FAMILY_FIND_HUB, "Find Hub"},
        {AMULET_BLE_TRACKER_FAMILY_DULT, "DULT"},
        {AMULET_BLE_TRACKER_FAMILY_TILE, "Tile"},
    };
    size_t used = 0;
    text[0] = '\0';
    for (size_t index = 0; index < sizeof(names) / sizeof(names[0]); index++) {
        if ((families & names[index].family) != 0 && used < size) {
            int written = snprintf(
                text + used,
                size - used,
                "%s%s",
                used > 0 ? ", " : "",
                names[index].name
            );
            if (written > 0) {
                used += (size_t)written;
            }
        }
    }
}

// One dim double-blink color per tracker species, so attendees can look a
// blink up in the field guide. Red, blue and white are taken by the fixture
// alert, broadcasting and the idle breathe.
typedef struct {
    uint8_t families;
    uint8_t red;
    uint8_t green;
    uint8_t blue;
} tracker_species_t;

static const tracker_species_t TRACKER_SPECIES[] = {
    {AMULET_BLE_TRACKER_FAMILY_FIND_MY, 0, 40, 6},          // green
    {AMULET_BLE_TRACKER_FAMILY_SMARTTAG, 0, 28, 32},        // cyan
    {AMULET_BLE_TRACKER_FAMILY_FIND_HUB |
         AMULET_BLE_TRACKER_FAMILY_DULT, 34, 30, 0},        // yellow
    {AMULET_BLE_TRACKER_FAMILY_TILE, 40, 10, 0},            // orange
};
#define TRACKER_SPECIES_COUNT (sizeof(TRACKER_SPECIES) / sizeof(TRACKER_SPECIES[0]))

// Gates and indicates provisional signals off the NimBLE host task. The gates
// see only signal kinds and times, never identities or addresses. Each
// species has its own gate, so a busy one cannot hide a rarer one.
static void signal_indicator_task(void *context)
{
    (void)context;
    amulet_signal_gate_t tracker_gates[TRACKER_SPECIES_COUNT];
    uint8_t tracker_families[TRACKER_SPECIES_COUNT] = {0};
    amulet_signal_gate_t glasses_gate;
    amulet_signal_gate_t drone_gate;
    amulet_signal_gate_t police_maker_gate;
    for (size_t index = 0; index < TRACKER_SPECIES_COUNT; index++) {
        amulet_signal_gate_init(&tracker_gates[index]);
    }
    amulet_signal_gate_init(&glasses_gate);
    amulet_signal_gate_init(&drone_gate);
    amulet_signal_gate_init(&police_maker_gate);

    while (true) {
        signal_report_t report;
        if (xQueueReceive(signal_report_queue, &report, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        unsigned int dropped = atomic_exchange(&signal_report_drop_count, 0);
        if (dropped > 0) {
            ESP_LOGW(TAG, "provisional signals: %u report(s) dropped", dropped);
        }

        if ((report.signals & AMULET_BLE_SIGNAL_TRACKER_SEPARATED) != 0) {
            for (size_t index = 0; index < TRACKER_SPECIES_COUNT; index++) {
                const tracker_species_t *species = &TRACKER_SPECIES[index];
                uint8_t matched = report.tracker_families & species->families;
                if (matched == 0) {
                    continue;
                }
                tracker_families[index] |= matched;
                if (!amulet_signal_gate_report(&tracker_gates[index], report.seen_us)) {
                    continue;
                }
                char families[48];
                describe_tracker_families(tracker_families[index], families, sizeof(families));
                ESP_LOGI(TAG, "provisional: tracker-compatible frame away from its owner nearby (%s)",
                         families);
                tracker_families[index] = 0;
                blink_signal_indicator(
                    SIGNAL_SPECIES_BLINKS,
                    species->red,
                    species->green,
                    species->blue
                );
            }
        }
        if ((report.signals & AMULET_BLE_SIGNAL_SMART_GLASSES) != 0 &&
            amulet_signal_gate_report(&glasses_gate, report.seen_us)) {
            ESP_LOGI(TAG, "provisional: device from a smart-glasses maker nearby");
            blink_signal_indicator(
                SIGNAL_SPECIES_BLINKS,
                SIGNAL_GLASSES_LED_RED,
                SIGNAL_GLASSES_LED_GREEN,
                SIGNAL_GLASSES_LED_BLUE
            );
        }
        if ((report.signals & AMULET_BLE_SIGNAL_DRONE_REMOTE_ID) != 0 &&
            amulet_signal_gate_report(&drone_gate, report.seen_us)) {
            ESP_LOGI(TAG, "provisional: drone Remote ID broadcast nearby");
            blink_signal_indicator(
                SIGNAL_SURVEILLANCE_BLINKS,
                SIGNAL_DRONE_LED_RED,
                SIGNAL_DRONE_LED_GREEN,
                SIGNAL_DRONE_LED_BLUE
            );
        }
        if ((report.signals & AMULET_BLE_SIGNAL_POLICE_EQUIPMENT_MAKER) != 0 &&
            amulet_signal_gate_report(&police_maker_gate, report.seen_us)) {
            ESP_LOGI(TAG, "provisional: device from a police-equipment maker nearby");
            blink_signal_indicator(
                SIGNAL_SURVEILLANCE_BLINKS,
                SIGNAL_POLICE_MAKER_LED_RED,
                SIGNAL_POLICE_MAKER_LED_GREEN,
                SIGNAL_POLICE_MAKER_LED_BLUE
            );
        }
    }
}

static void initialize_signal_indicator(void)
{
    signal_report_queue = xQueueCreate(SIGNAL_REPORT_QUEUE_LENGTH, sizeof(signal_report_t));
    ESP_ERROR_CHECK(signal_report_queue != NULL ? ESP_OK : ESP_ERR_NO_MEM);
    BaseType_t task_created = xTaskCreate(
        signal_indicator_task,
        "amulet_signals",
        3072,
        NULL,
        2,
        NULL
    );
    ESP_ERROR_CHECK(task_created == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
}
#endif

// Owns the hunt state off the NimBLE host task. It sees only beacon numbers,
// signal strength and times, and keeps nothing across a restart.
static void hunt_task(void *context)
{
    (void)context;
    amulet_hunt_t hunt;
    amulet_hunt_init(&hunt, CONFIG_AMULET_HUNT_NEAR_RSSI_DBM, HUNT_REARM_US);

    while (true) {
        hunt_report_t report;
        bool received = xQueueReceive(
            hunt_report_queue,
            &report,
            pdMS_TO_TICKS(1000)
        ) == pdTRUE;
        unsigned int celebrated = atomic_exchange(&hunt_celebrated_beacons, 0);
        for (uint8_t beacon = 1; beacon <= AMULET_HUNT_MAX_BEACONS; beacon++) {
            if ((celebrated & (1U << (beacon - 1U))) != 0) {
                amulet_hunt_celebrated(&hunt, beacon);
            }
        }
        if (!received ||
            !amulet_hunt_report(&hunt, report.beacon, report.rssi, report.seen_us)) {
            continue;
        }
        if (xQueueSend(hunt_alert_queue, &report.beacon, 0) == pdTRUE) {
            ESP_LOGI(TAG, "scavenger hunt: found beacon %u", (unsigned int)report.beacon);
        } else {
            amulet_hunt_cancel(&hunt, report.beacon);
        }
    }
}

static void initialize_hunt(void)
{
    hunt_report_queue = xQueueCreate(HUNT_REPORT_QUEUE_LENGTH, sizeof(hunt_report_t));
    ESP_ERROR_CHECK(hunt_report_queue != NULL ? ESP_OK : ESP_ERR_NO_MEM);
    BaseType_t task_created = xTaskCreate(
        hunt_task,
        "amulet_hunt",
        2560,
        NULL,
        2,
        NULL
    );
    ESP_ERROR_CHECK(task_created == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
}

// Sightings are frequent while near a beacon, so a dropped one costs nothing.
static void queue_hunt_report(uint8_t beacon, int8_t rssi)
{
    const hunt_report_t report = {
        .seen_us = esp_timer_get_time(),
        .beacon = beacon,
        .rssi = rssi,
    };
    xQueueSend(hunt_report_queue, &report, 0);
}

static int gap_event(struct ble_gap_event *event, void *context)
{
    (void)context;

    switch (event->type) {
    case BLE_GAP_EVENT_DISC: {
        amulet_ble_advertisement_t advertisement;
        // NimBLE stores addresses least significant byte first; the
        // classifier sees only a public address's three-byte prefix.
        const amulet_ble_scan_metadata_t metadata = {
            .pdu_type = event->disc.event_type ==
                    BLE_HCI_ADV_RPT_EVTYPE_NONCONN_IND
                ? AMULET_BLE_PDU_TYPE_ADV_NONCONN_IND
                : AMULET_BLE_PDU_TYPE_OTHER,
            .public_address = event->disc.addr.type == BLE_ADDR_PUBLIC,
            .address_prefix = {
                event->disc.addr.val[5],
                event->disc.addr.val[4],
                event->disc.addr.val[3],
            },
        };
        bool valid = amulet_ble_classify_advertisement(
            event->disc.data,
            event->disc.length_data,
            &metadata,
            &advertisement
        );
        if (valid) {
            switch (amulet_ble_primary_action(&advertisement)) {
            case AMULET_BLE_ACTION_TEST_TRACKER:
                handle_test_tracker(&event->disc);
                break;
            case AMULET_BLE_ACTION_PEER_AMULET:
                handle_peer_amulet(&event->disc);
                break;
            case AMULET_BLE_ACTION_AMBIENT: {
                int64_t seen_us = esp_timer_get_time();
                for (uint8_t index = 0;
                     index < advertisement.ambient_marker_count;
                     index++) {
                    amulet_ambient_submit(
                        &advertisement.ambient_markers[index],
                        event->disc.addr.val,
                        event->disc.addr.type,
                        event->disc.rssi,
                        seen_us
                    );
                }
                break;
            }
            case AMULET_BLE_ACTION_NONE:
                break;
            }
#ifdef CONFIG_AMULET_PROVISIONAL_SIGNATURES
            if (advertisement.signals != 0) {
                queue_signal_report(&advertisement);
            }
#endif
            if (advertisement.hunt_beacon != 0) {
                queue_hunt_report(advertisement.hunt_beacon, event->disc.rssi);
            }
            amulet_ble_clear_advertisement(&advertisement);
        }
        return 0;
    }
    case BLE_GAP_EVENT_DISC_COMPLETE:
        ESP_LOGW(TAG, "scan stopped with reason=%d; restarting", event->disc_complete.reason);
        ensure_scan_started();
        return 0;
    default:
        return 0;
    }
}

static int start_scan(void)
{
    uint8_t own_address_type;
    int rc = ble_hs_id_infer_auto(0, &own_address_type);
    if (rc != 0) {
        ESP_LOGE(TAG, "could not determine BLE address type; rc=%d", rc);
        return rc;
    }

    const struct ble_gap_disc_params scan_parameters = {
        .itvl = 0,
        .window = 0,
        .filter_policy = 0,
        .limited = 0,
        .passive = 1,
        .filter_duplicates = 0,
    };

    rc = ble_gap_disc(
        own_address_type,
        BLE_HS_FOREVER,
        &scan_parameters,
        gap_event,
        NULL
    );
    if (rc == BLE_HS_EALREADY) {
        return 0;
    }
    if (rc != 0) {
        ESP_LOGE(TAG, "could not start passive BLE scan; rc=%d", rc);
        return rc;
    }

    ESP_LOGI(TAG, "scanning for test payload ff ff 54 52 4b 52 01");
    return 0;
}

static void scan_retry_task(void *context)
{
    (void)context;

    while (true) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        uint32_t retry_count = 0;

        while (!ble_gap_disc_active()) {
            vTaskDelay(pdMS_TO_TICKS(SCAN_RETRY_DELAY_MS));
            if (!ble_hs_synced()) {
                continue;
            }

            retry_count++;
            if (start_scan() == 0) {
                ESP_LOGI(TAG, "passive BLE scan recovered after %lu retry attempt(s)",
                         (unsigned long)retry_count);
                break;
            }
        }
    }
}

static void ensure_scan_started(void)
{
    if (ble_gap_disc_active() || start_scan() == 0) {
        return;
    }

    ESP_LOGW(TAG, "passive BLE scan restart failed; retrying in %d ms",
             SCAN_RETRY_DELAY_MS);
    xTaskNotifyGive(scan_retry_task_handle);
}

static void initialize_scan_recovery(void)
{
    BaseType_t task_created = xTaskCreate(
        scan_retry_task,
        "scan_retry",
        2048,
        NULL,
        5,
        &scan_retry_task_handle
    );
    ESP_ERROR_CHECK(task_created == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
}

static bool finish_peer_advertising(void)
{
    int expected = OUTGOING_ADVERTISING;
    if (!atomic_compare_exchange_strong(
            &outgoing_state,
            &expected,
            OUTGOING_FINISHING
        )) {
        return false;
    }

    discard_pending_responses();
    set_peer_advertising_active(false);
    if (atomic_load(&ble_ready) && !ble_gap_disc_active()) {
        ensure_scan_started();
    }
    atomic_store(&outgoing_state, OUTGOING_IDLE);
    return true;
}

static int advertising_event(struct ble_gap_event *event, void *context)
{
    (void)context;

    if (event->type != BLE_GAP_EVENT_ADV_COMPLETE) {
        return 0;
    }

    if (finish_peer_advertising()) {
        if (event->adv_complete.reason == BLE_HS_ETIMEOUT) {
            ESP_LOGI(TAG, "peer advertising complete");
        } else {
            ESP_LOGW(
                TAG,
                "peer advertising stopped with reason=%d",
                event->adv_complete.reason
            );
        }
    }
    return 0;
}

static bool start_peer_advertising(TickType_t *started_at)
{
    if (!atomic_load(&ble_ready)) {
        ESP_LOGW(TAG, "BLE host is not ready after local call");
        clear_outgoing_state(OUTGOING_CALL);
        return false;
    }
    if (ble_gap_adv_active()) {
        ESP_LOGI(TAG, "peer advertising is already active");
        clear_outgoing_state(OUTGOING_CALL);
        return false;
    }
    if (!ble_gap_disc_active()) {
        ESP_LOGW(TAG, "BLE scan is not ready after local call");
        clear_outgoing_state(OUTGOING_CALL);
        return false;
    }

    const struct ble_hs_adv_fields fields = {
        .flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP,
        .uuids128 = &PEER_SERVICE_UUID,
        .num_uuids128 = 1,
        .uuids128_is_complete = 1,
    };
    int rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "could not set peer advertisement data; rc=%d", rc);
        clear_outgoing_state(OUTGOING_CALL);
        return false;
    }

    ble_addr_t address;
    rc = ble_hs_id_gen_rnd(1, &address);
    if (rc != 0) {
        ESP_LOGE(TAG, "could not generate private BLE address; rc=%d", rc);
        clear_outgoing_state(OUTGOING_CALL);
        return false;
    }

    rc = ble_gap_disc_cancel();
    if (rc != 0) {
        ESP_LOGE(TAG, "could not pause BLE scan for address rotation; rc=%d", rc);
        clear_outgoing_state(OUTGOING_CALL);
        return false;
    }

    rc = ble_hs_id_set_rnd(address.val);
    if (rc != 0) {
        ESP_LOGE(TAG, "could not set private BLE address; rc=%d", rc);
        ensure_scan_started();
        clear_outgoing_state(OUTGOING_CALL);
        return false;
    }

    const struct ble_gap_adv_params parameters = {
        .conn_mode = BLE_GAP_CONN_MODE_NON,
        .disc_mode = BLE_GAP_DISC_MODE_GEN,
        .itvl_min = BLE_GAP_ADV_ITVL_MS(PEER_ADVERTISEMENT_INTERVAL_MS),
        .itvl_max = BLE_GAP_ADV_ITVL_MS(PEER_ADVERTISEMENT_INTERVAL_MS),
    };
    int expected = OUTGOING_CALL;
    if (!atomic_compare_exchange_strong(
            &outgoing_state,
            &expected,
            OUTGOING_STARTING
        )) {
        ensure_scan_started();
        return false;
    }
    rc = ble_gap_adv_start(
        BLE_OWN_ADDR_RANDOM,
        NULL,
        PEER_ADVERTISEMENT_DURATION_MS,
        &parameters,
        advertising_event,
        NULL
    );
    if (rc != 0) {
        ESP_LOGE(TAG, "could not start peer advertising; rc=%d", rc);
        ensure_scan_started();
        clear_outgoing_state(OUTGOING_STARTING);
        return false;
    }

    expected = OUTGOING_STARTING;
    if (!atomic_compare_exchange_strong(
            &outgoing_state,
            &expected,
            OUTGOING_ADVERTISING
        )) {
        int stop_error = ble_gap_adv_stop();
        if (stop_error != 0 && stop_error != BLE_HS_EALREADY) {
            ESP_LOGW(TAG, "could not stop an interrupted advertisement; rc=%d",
                     stop_error);
        }
        ensure_scan_started();
        return false;
    }

    set_peer_advertising_active(true);
    if (atomic_load(&outgoing_state) != OUTGOING_ADVERTISING) {
        set_peer_advertising_active(false);
        ble_gap_adv_stop();
        ensure_scan_started();
        return false;
    }
    *started_at = xTaskGetTickCount();

    ESP_LOGI(
        TAG,
        "peer advertising started: uuid=33dbba50-c8ae-4344-a905-3230af113634 "
        "address=%02x:%02x:%02x:%02x:%02x:%02x duration=%d ms",
        address.val[5], address.val[4], address.val[3],
        address.val[2], address.val[1], address.val[0],
        PEER_ADVERTISEMENT_DURATION_MS
    );

    // The C3 resumes scan reports after its local legacy advertisement ends.
    ensure_scan_started();
    return true;
}

static void advertising_task(void *context)
{
    (void)context;

    while (true) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        TickType_t started_at;
        if (!start_peer_advertising(&started_at)) {
            continue;
        }

        vTaskDelayUntil(
            &started_at,
            pdMS_TO_TICKS(PEER_ADVERTISEMENT_DURATION_MS + 20)
        );
        while (atomic_load(&outgoing_state) == OUTGOING_ADVERTISING) {
            int rc = ble_gap_adv_stop();
            if (rc == 0 || rc == BLE_HS_EALREADY) {
                if (finish_peer_advertising()) {
                    ESP_LOGW(TAG, "peer advertising timeout was late; stopped explicitly");
                    ESP_LOGI(TAG, "peer advertising complete");
                }
                break;
            }
            ESP_LOGW(TAG, "could not stop peer advertising; rc=%d; retrying", rc);
            vTaskDelay(pdMS_TO_TICKS(SCAN_RETRY_DELAY_MS));
        }
    }
}

static void initialize_advertising_task(void)
{
    BaseType_t task_created = xTaskCreate(
        advertising_task,
        "peer_radio",
        3072,
        NULL,
        5,
        &advertising_task_handle
    );
    ESP_ERROR_CHECK(task_created == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
}

static void on_ble_reset(int reason)
{
    atomic_store(&ble_ready, false);
    clear_outgoing_state(OUTGOING_STARTING);
    clear_outgoing_state(OUTGOING_ADVERTISING);
    set_peer_advertising_active(false);
    ESP_LOGE(TAG, "NimBLE host reset; reason=%d", reason);
}

static void on_ble_sync(void)
{
    int rc = ble_hs_util_ensure_addr(0);
    if (rc != 0) {
        ESP_LOGE(TAG, "could not initialize a BLE address; rc=%d", rc);
        return;
    }

    ensure_scan_started();
    atomic_store(&ble_ready, true);
}

static void nimble_host_task(void *context)
{
    (void)context;
    ESP_LOGI(TAG, "NimBLE host started");
    nimble_port_run();
    nimble_port_freertos_deinit();
}

#if CONFIG_AMULET_HUNT_BEACON_NUMBER > 0
static const uint8_t HUNT_BEACON_DATA[AMULET_HUNT_BEACON_DATA_LENGTH] = {
    0xff,
    0xff,
    AMULET_HUNT_BEACON_TYPE,
    AMULET_HUNT_BEACON_VERSION,
    CONFIG_AMULET_HUNT_BEACON_NUMBER,
};

#if defined(CONFIG_AMULET_HUNT_BEACON_TX_POWER_LOW)
#define HUNT_BEACON_TX_POWER ESP_PWR_LVL_N12
#elif defined(CONFIG_AMULET_HUNT_BEACON_TX_POWER_HIGH)
#define HUNT_BEACON_TX_POWER ESP_PWR_LVL_P9
#else
#define HUNT_BEACON_TX_POWER ESP_PWR_LVL_N0
#endif

static void start_hunt_beacon_advertising(void);

static int hunt_beacon_event(struct ble_gap_event *event, void *context)
{
    (void)context;
    if (event->type == BLE_GAP_EVENT_ADV_COMPLETE) {
        ESP_LOGW(TAG, "hunt beacon advertising stopped with reason=%d; restarting",
                 event->adv_complete.reason);
        start_hunt_beacon_advertising();
    }
    return 0;
}

static void start_hunt_beacon_advertising(void)
{
    uint8_t own_address_type;
    int rc = ble_hs_id_infer_auto(0, &own_address_type);
    if (rc != 0) {
        ESP_LOGE(TAG, "could not determine BLE address type; rc=%d", rc);
        return;
    }
    const struct ble_hs_adv_fields fields = {
        .flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP,
        .mfg_data = HUNT_BEACON_DATA,
        .mfg_data_len = sizeof(HUNT_BEACON_DATA),
    };
    rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "could not set hunt beacon data; rc=%d", rc);
        return;
    }
    const struct ble_gap_adv_params parameters = {
        .conn_mode = BLE_GAP_CONN_MODE_NON,
        .disc_mode = BLE_GAP_DISC_MODE_GEN,
        .itvl_min = BLE_GAP_ADV_ITVL_MS(HUNT_BEACON_ADVERTISEMENT_INTERVAL_MS),
        .itvl_max = BLE_GAP_ADV_ITVL_MS(HUNT_BEACON_ADVERTISEMENT_INTERVAL_MS),
    };
    rc = ble_gap_adv_start(
        own_address_type,
        NULL,
        BLE_HS_FOREVER,
        &parameters,
        hunt_beacon_event,
        NULL
    );
    if (rc != 0) {
        ESP_LOGE(TAG, "could not start hunt beacon advertising; rc=%d", rc);
        return;
    }
    ESP_LOGI(TAG, "scavenger hunt beacon %d advertising every %d ms",
             CONFIG_AMULET_HUNT_BEACON_NUMBER, HUNT_BEACON_ADVERTISEMENT_INTERVAL_MS);
}

static void on_hunt_beacon_sync(void)
{
    int rc = ble_hs_util_ensure_addr(0);
    if (rc != 0) {
        ESP_LOGE(TAG, "could not initialize a BLE address; rc=%d", rc);
        return;
    }
    esp_err_t error = esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV, HUNT_BEACON_TX_POWER);
    if (error != ESP_OK) {
        ESP_LOGW(TAG, "could not set hunt beacon transmit power; error=%s",
                 esp_err_to_name(error));
    }
    start_hunt_beacon_advertising();
}

static void on_hunt_beacon_reset(int reason)
{
    ESP_LOGE(TAG, "NimBLE host reset; reason=%d", reason);
}

// A slow rainbow so people can spot the beacon itself.
static void hunt_beacon_glow_task(void *context)
{
    (void)context;
    TickType_t last_wake = xTaskGetTickCount();
    for (;;) {
        uint32_t phase_ms = (uint32_t)((esp_timer_get_time() / 1000) % HUNT_BEACON_GLOW_SWEEP_MS);
        uint8_t red;
        uint8_t green;
        uint8_t blue;
        amulet_hunt_rainbow_rgb(
            phase_ms * AMULET_HUNT_HUE_STEPS / HUNT_BEACON_GLOW_SWEEP_MS,
            HUNT_BEACON_GLOW_LEVEL,
            &red,
            &green,
            &blue
        );
        ESP_ERROR_CHECK(led_strip_set_pixel(led_strip, 0, red, green, blue));
        ESP_ERROR_CHECK(led_strip_refresh(led_strip));
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(HUNT_RAINBOW_STEP_MS));
    }
}

// A beacon never scans, calls, responds or sounds; the buzzer is configured
// only so its pin is held silent.
static void run_hunt_beacon(void)
{
    initialize_buzzer();
    initialize_led_strip();
    BaseType_t task_created = xTaskCreate(
        hunt_beacon_glow_task,
        "hunt_glow",
        2048,
        NULL,
        1,
        NULL
    );
    ESP_ERROR_CHECK(task_created == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);

    esp_err_t error = nimble_port_init();
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "could not initialize NimBLE; error=%s", esp_err_to_name(error));
        return;
    }
    ble_hs_cfg.reset_cb = on_hunt_beacon_reset;
    ble_hs_cfg.sync_cb = on_hunt_beacon_sync;
    nimble_port_freertos_init(nimble_host_task);
}
#endif

void app_main(void)
{
    esp_err_t error = nvs_flash_init();
    if (error == ESP_ERR_NVS_NO_FREE_PAGES || error == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        error = nvs_flash_init();
    }
    ESP_ERROR_CHECK(error);
#if CONFIG_AMULET_HUNT_BEACON_NUMBER > 0
    run_hunt_beacon();
    return;
#endif
    ESP_ERROR_CHECK(amulet_config_init());

    initialize_buzzer();
    initialize_led();
    initialize_hunt();
#ifdef CONFIG_AMULET_PROVISIONAL_SIGNATURES
    initialize_signal_indicator();
#endif
    initialize_peer_response_scheduler();
    initialize_scan_recovery();
    initialize_advertising_task();
    initialize_button();
    ESP_ERROR_CHECK(amulet_serial_init(queue_response_preview));

    error = nimble_port_init();
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "could not initialize NimBLE; error=%s", esp_err_to_name(error));
        return;
    }
    ESP_ERROR_CHECK(amulet_ambient_runtime_init());

    ble_hs_cfg.reset_cb = on_ble_reset;
    ble_hs_cfg.sync_cb = on_ble_sync;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
    ble_store_config_init();
    nimble_port_freertos_init(nimble_host_task);
}
