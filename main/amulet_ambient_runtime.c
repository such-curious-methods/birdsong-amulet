#include "amulet_ambient_runtime.h"

#include <limits.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "amulet_ambient_observer.h"
#include "amulet_ble_classifier.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "mbedtls/md.h"

#define AMBIENT_REPORT_QUEUE_LENGTH 64U
#define AMBIENT_BOOT_KEY_LENGTH 32U
#define AMBIENT_HMAC_LENGTH 32U
#define AMBIENT_HMAC_INPUT_LENGTH 64U
#define AMBIENT_MAINTENANCE_INTERVAL_MS 30000U
#define AMBIENT_OBSERVATION_EXPIRY_US (10LL * 60LL * 1000LL * 1000LL)
#define AMBIENT_NOTICE_INTERVAL_US (30LL * 1000LL * 1000LL)
#define BLE_RSSI_UNKNOWN 127

static const char *TAG = "ambient";
static const char IBEACON_IDENTITY_DOMAIN[] = "ibeacon-identity-v1";
static const char ALTBEACON_IDENTITY_DOMAIN[] = "altbeacon-identity-v1";
static const char BLE_ADDRESS_DOMAIN[] = "ble-address-v1";

typedef struct {
    amulet_ble_ambient_marker_t marker;
    uint8_t address[6];
    int64_t seen_us;
    uint8_t address_type;
    int8_t rssi;
} ambient_report_t;

_Static_assert(
    AMBIENT_REPORT_QUEUE_LENGTH <= UINT8_MAX,
    "ambient report slots must fit in the queue index"
);

static QueueHandle_t ambient_free_slot_queue;
static QueueHandle_t ambient_report_queue;
static ambient_report_t ambient_report_pool[AMBIENT_REPORT_QUEUE_LENGTH];
static mbedtls_md_context_t ambient_hmac_context;
static atomic_uint ambient_report_drop_count;
static atomic_uint ambient_altbeacon_shaped_count;
static atomic_uint ambient_altbeacon_conflict_count;
static atomic_uint ambient_multiplicity_conflict_count;
static atomic_uint ambient_candidate_overflow_count;
static amulet_ambient_table_t ambient_table;

static void clear_sensitive(void *data, size_t length)
{
    volatile uint8_t *bytes = data;
    while (length > 0) {
        *bytes++ = 0;
        length--;
    }
}

static bool derive_key(
    const char *domain,
    const uint8_t *data,
    size_t data_length,
    uint8_t output[AMULET_AMBIENT_KEY_LENGTH]
)
{
    size_t domain_length = strlen(domain) + 1;
    if (domain_length + data_length > AMBIENT_HMAC_INPUT_LENGTH) {
        return false;
    }

    uint8_t input[AMBIENT_HMAC_INPUT_LENGTH] = {0};
    uint8_t digest[AMBIENT_HMAC_LENGTH];
    memcpy(input, domain, domain_length);
    memcpy(input + domain_length, data, data_length);
    int rc = mbedtls_md_hmac_reset(&ambient_hmac_context);
    if (rc == 0) {
        rc = mbedtls_md_hmac_update(
            &ambient_hmac_context,
            input,
            domain_length + data_length
        );
    }
    if (rc == 0) {
        rc = mbedtls_md_hmac_finish(&ambient_hmac_context, digest);
    }
    if (rc == 0) {
        memcpy(output, digest, AMULET_AMBIENT_KEY_LENGTH);
    }
    clear_sensitive(input, sizeof(input));
    clear_sensitive(digest, sizeof(digest));
    return rc == 0;
}

static void log_dropped_reports(int64_t now_us)
{
    static unsigned int pending_drops;
    static int64_t last_log_us = -AMBIENT_NOTICE_INTERVAL_US;
    unsigned int dropped = atomic_exchange(&ambient_report_drop_count, 0);
    if (UINT_MAX - pending_drops < dropped) {
        pending_drops = UINT_MAX;
    } else {
        pending_drops += dropped;
    }
    if (pending_drops > 0 && now_us - last_log_us >= AMBIENT_NOTICE_INTERVAL_US) {
        ESP_LOGW(TAG, "ambient queue dropped %u report(s)", pending_drops);
        pending_drops = 0;
        last_log_us = now_us;
    }
}

static void release_report_slot(uint8_t slot)
{
    clear_sensitive(&ambient_report_pool[slot], sizeof(ambient_report_pool[slot]));
    BaseType_t released = xQueueSend(ambient_free_slot_queue, &slot, 0);
    configASSERT(released == pdTRUE);
    (void)released;
}

static const char *identity_domain(amulet_ble_rule_t rule)
{
    switch (rule) {
    case AMULET_BLE_RULE_IBEACON:
        return IBEACON_IDENTITY_DOMAIN;
    case AMULET_BLE_RULE_ALTBEACON:
        return ALTBEACON_IDENTITY_DOMAIN;
    case AMULET_BLE_RULE_NONE:
    case AMULET_BLE_RULE_TEST_TRACKER:
    case AMULET_BLE_RULE_PEER_AMULET:
    default:
        return NULL;
    }
}

static const char *marker_label(
    amulet_ble_rule_t rule,
    amulet_ble_ambient_evidence_t evidence
)
{
    if (rule == AMULET_BLE_RULE_IBEACON) {
        return "iBeacon-format";
    }
    if (rule == AMULET_BLE_RULE_ALTBEACON &&
        evidence == AMULET_BLE_AMBIENT_EVIDENCE_ALTBEACON_STRICT_V1) {
        return "AltBeacon v1.0";
    }
    if (rule == AMULET_BLE_RULE_ALTBEACON) {
        return "AltBeacon-format";
    }
    return "BLE-format";
}

static bool logical_marker_is_valid(const amulet_ble_ambient_marker_t *marker)
{
    if (marker == NULL ||
        marker->observation_kind !=
            AMULET_BLE_AMBIENT_OBSERVATION_LOGICAL_MARKER ||
        (marker->multiplicity != AMULET_BLE_CANDIDATE_MULTIPLICITY_ONE &&
         marker->multiplicity !=
            AMULET_BLE_CANDIDATE_MULTIPLICITY_REPEATED_IDENTICAL) ||
        marker->category != AMULET_BLE_CATEGORY_LOCATION_INFRASTRUCTURE ||
        marker->confidence != AMULET_BLE_CONFIDENCE_STRUCTURAL ||
        !marker->calibration_power_valid) {
        return false;
    }

    switch (marker->rule) {
    case AMULET_BLE_RULE_IBEACON:
        return marker->identity_length == AMULET_IBEACON_IDENTITY_LENGTH &&
               marker->evidence == AMULET_BLE_AMBIENT_EVIDENCE_IBEACON_FORMAT;
    case AMULET_BLE_RULE_ALTBEACON:
        return marker->identity_length == AMULET_ALTBEACON_IDENTITY_LENGTH &&
               (marker->evidence ==
                    AMULET_BLE_AMBIENT_EVIDENCE_ALTBEACON_STRICT_V1 ||
                marker->evidence ==
                    AMULET_BLE_AMBIENT_EVIDENCE_ALTBEACON_PAYLOAD_MATCH);
    case AMULET_BLE_RULE_NONE:
    case AMULET_BLE_RULE_TEST_TRACKER:
    case AMULET_BLE_RULE_PEER_AMULET:
    default:
        return false;
    }
}

static void record_marker_diagnostic(const amulet_ble_ambient_marker_t *marker)
{
    if (marker->multiplicity ==
        AMULET_BLE_CANDIDATE_MULTIPLICITY_MULTIPLE_DISTINCT) {
        atomic_fetch_add(&ambient_multiplicity_conflict_count, 1);
        return;
    }
    if (marker->multiplicity == AMULET_BLE_CANDIDATE_MULTIPLICITY_OVERFLOW) {
        atomic_fetch_add(&ambient_candidate_overflow_count, 1);
        return;
    }

    switch (marker->evidence) {
    case AMULET_BLE_AMBIENT_EVIDENCE_ALTBEACON_ENVELOPE_CONFLICT:
        atomic_fetch_add(&ambient_altbeacon_conflict_count, 1);
        break;
    case AMULET_BLE_AMBIENT_EVIDENCE_ALTBEACON_SHAPED:
        atomic_fetch_add(&ambient_altbeacon_shaped_count, 1);
        break;
    case AMULET_BLE_AMBIENT_EVIDENCE_NONE:
    case AMULET_BLE_AMBIENT_EVIDENCE_IBEACON_FORMAT:
    case AMULET_BLE_AMBIENT_EVIDENCE_ALTBEACON_STRICT_V1:
    case AMULET_BLE_AMBIENT_EVIDENCE_ALTBEACON_PAYLOAD_MATCH:
    default:
        break;
    }
}

static unsigned int saturating_add(unsigned int left, unsigned int right)
{
    return UINT_MAX - left < right ? UINT_MAX : left + right;
}

static void log_marker_diagnostics(int64_t now_us)
{
    static unsigned int pending_shaped;
    static unsigned int pending_conflicts;
    static unsigned int pending_multiplicity;
    static unsigned int pending_overflow;
    static int64_t last_log_us = -AMBIENT_NOTICE_INTERVAL_US;

    pending_shaped = saturating_add(
        pending_shaped,
        atomic_exchange(&ambient_altbeacon_shaped_count, 0)
    );
    pending_conflicts = saturating_add(
        pending_conflicts,
        atomic_exchange(&ambient_altbeacon_conflict_count, 0)
    );
    pending_multiplicity = saturating_add(
        pending_multiplicity,
        atomic_exchange(&ambient_multiplicity_conflict_count, 0)
    );
    pending_overflow = saturating_add(
        pending_overflow,
        atomic_exchange(&ambient_candidate_overflow_count, 0)
    );
    if ((pending_shaped == 0 && pending_conflicts == 0 &&
         pending_multiplicity == 0 && pending_overflow == 0) ||
        now_us - last_log_us < AMBIENT_NOTICE_INTERVAL_US) {
        return;
    }

    ESP_LOGI(
        TAG,
        "ambient candidates ignored: altbeacon_shaped=%u "
        "altbeacon_pdu_conflict=%u multiple_distinct=%u overflow=%u",
        pending_shaped,
        pending_conflicts,
        pending_multiplicity,
        pending_overflow
    );
    pending_shaped = 0;
    pending_conflicts = 0;
    pending_multiplicity = 0;
    pending_overflow = 0;
    last_log_us = now_us;
}

static void ambient_observation_task(void *context)
{
    (void)context;
    amulet_ambient_table_init(&ambient_table);
    int64_t last_notice_us = -AMBIENT_NOTICE_INTERVAL_US;
    bool digest_error_logged = false;

    while (true) {
        uint8_t slot;
        if (xQueueReceive(
                ambient_report_queue,
                &slot,
                pdMS_TO_TICKS(AMBIENT_MAINTENANCE_INTERVAL_MS)
            ) != pdTRUE) {
            int64_t now_us = esp_timer_get_time();
            amulet_ambient_expire(
                &ambient_table,
                now_us,
                AMBIENT_OBSERVATION_EXPIRY_US
            );
            log_marker_diagnostics(now_us);
            log_dropped_reports(now_us);
            continue;
        }
        configASSERT(slot < AMBIENT_REPORT_QUEUE_LENGTH);
        ambient_report_t *report = &ambient_report_pool[slot];

        if (report->marker.observation_kind ==
            AMULET_BLE_AMBIENT_OBSERVATION_DIAGNOSTIC) {
            int64_t seen_us = report->seen_us;
            record_marker_diagnostic(&report->marker);
            release_report_slot(slot);
            amulet_ambient_expire(
                &ambient_table,
                seen_us,
                AMBIENT_OBSERVATION_EXPIRY_US
            );
            log_marker_diagnostics(seen_us);
            log_dropped_reports(esp_timer_get_time());
            continue;
        }

        const char *domain = identity_domain(report->marker.rule);
        bool marker_valid = logical_marker_is_valid(&report->marker) &&
            domain != NULL;
        if (!marker_valid) {
            release_report_slot(slot);
            atomic_fetch_add(&ambient_report_drop_count, 1);
            int64_t now_us = esp_timer_get_time();
            amulet_ambient_expire(
                &ambient_table,
                now_us,
                AMBIENT_OBSERVATION_EXPIRY_US
            );
            log_marker_diagnostics(now_us);
            log_dropped_reports(now_us);
            continue;
        }

        uint8_t address_material[7] = {report->address_type};
        memcpy(address_material + 1, report->address, sizeof(report->address));
        amulet_ambient_event_t event = {
            .seen_us = report->seen_us,
            .rule = report->marker.rule,
            .category = report->marker.category,
            .confidence = report->marker.confidence,
            .rssi = report->rssi,
            .rssi_valid = report->rssi != BLE_RSSI_UNKNOWN,
        };
        amulet_ble_ambient_evidence_t evidence = report->marker.evidence;
        bool digests_ready = derive_key(
            domain,
            report->marker.identity,
            report->marker.identity_length,
            event.entity_key
        ) && derive_key(
            BLE_ADDRESS_DOMAIN,
            address_material,
            sizeof(address_material),
            event.address_key
        );
        clear_sensitive(address_material, sizeof(address_material));
        release_report_slot(slot);
        if (!digests_ready) {
            if (!digest_error_logged) {
                ESP_LOGE(TAG, "could not derive boot-scoped ambient identifier");
                digest_error_logged = true;
            }
            clear_sensitive(&event, sizeof(event));
            int64_t now_us = esp_timer_get_time();
            amulet_ambient_expire(
                &ambient_table,
                now_us,
                AMBIENT_OBSERVATION_EXPIRY_US
            );
            log_marker_diagnostics(now_us);
            log_dropped_reports(now_us);
            continue;
        }

        amulet_ambient_entry_t *entry = NULL;
        amulet_ambient_record_result_t result = amulet_ambient_record(
            &ambient_table,
            &event,
            AMBIENT_OBSERVATION_EXPIRY_US,
            &entry
        );
        if ((result == AMULET_AMBIENT_RECORD_CREATED ||
             result == AMULET_AMBIENT_RECORD_REPLACED) &&
            entry != NULL) {
            int64_t notice_us = esp_timer_get_time();
            if (notice_us - last_notice_us >= AMBIENT_NOTICE_INTERVAL_US) {
                int8_t median_rssi = amulet_ambient_median_rssi(entry);
                const char *label = marker_label(entry->rule, evidence);
                if (median_rssi == AMULET_AMBIENT_RSSI_UNAVAILABLE) {
                    ESP_LOGI(
                        TAG,
                        "%s location marker observed: "
                        "id=%02x%02x%02x%02x%02x%02x rssi=unavailable",
                        label,
                        entry->entity_key[0], entry->entity_key[1],
                        entry->entity_key[2], entry->entity_key[3],
                        entry->entity_key[4], entry->entity_key[5]
                    );
                } else {
                    ESP_LOGI(
                        TAG,
                        "%s location marker observed: "
                        "id=%02x%02x%02x%02x%02x%02x rssi=%d dBm",
                        label,
                        entry->entity_key[0], entry->entity_key[1],
                        entry->entity_key[2], entry->entity_key[3],
                        entry->entity_key[4], entry->entity_key[5],
                        (int)median_rssi
                    );
                }
                last_notice_us = notice_us;
            }
        }
        clear_sensitive(&event, sizeof(event));
        log_marker_diagnostics(esp_timer_get_time());
        log_dropped_reports(esp_timer_get_time());
    }
}

esp_err_t amulet_ambient_runtime_init(void)
{
    const mbedtls_md_info_t *hmac_info =
        mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (hmac_info == NULL) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    uint8_t boot_key[AMBIENT_BOOT_KEY_LENGTH];
    esp_fill_random(boot_key, sizeof(boot_key));
    mbedtls_md_init(&ambient_hmac_context);
    int rc = mbedtls_md_setup(&ambient_hmac_context, hmac_info, 1);
    if (rc == 0) {
        rc = mbedtls_md_hmac_starts(
            &ambient_hmac_context,
            boot_key,
            sizeof(boot_key)
        );
    }
    clear_sensitive(boot_key, sizeof(boot_key));
    if (rc != 0) {
        mbedtls_md_free(&ambient_hmac_context);
        return ESP_FAIL;
    }

    ambient_report_queue = xQueueCreate(
        AMBIENT_REPORT_QUEUE_LENGTH,
        sizeof(uint8_t)
    );
    ambient_free_slot_queue = xQueueCreate(
        AMBIENT_REPORT_QUEUE_LENGTH,
        sizeof(uint8_t)
    );
    if (ambient_report_queue == NULL || ambient_free_slot_queue == NULL) {
        if (ambient_report_queue != NULL) {
            vQueueDelete(ambient_report_queue);
            ambient_report_queue = NULL;
        }
        if (ambient_free_slot_queue != NULL) {
            vQueueDelete(ambient_free_slot_queue);
            ambient_free_slot_queue = NULL;
        }
        mbedtls_md_free(&ambient_hmac_context);
        return ESP_ERR_NO_MEM;
    }
    for (uint8_t slot = 0; slot < AMBIENT_REPORT_QUEUE_LENGTH; slot++) {
        BaseType_t added = xQueueSend(ambient_free_slot_queue, &slot, 0);
        configASSERT(added == pdTRUE);
        (void)added;
    }

    BaseType_t task_created = xTaskCreate(
        ambient_observation_task,
        "ambient_observer",
        4096,
        NULL,
        5,
        NULL
    );
    if (task_created != pdPASS) {
        vQueueDelete(ambient_report_queue);
        vQueueDelete(ambient_free_slot_queue);
        ambient_report_queue = NULL;
        ambient_free_slot_queue = NULL;
        mbedtls_md_free(&ambient_hmac_context);
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void amulet_ambient_submit(
    const amulet_ble_ambient_marker_t *marker,
    const uint8_t address[6],
    uint8_t address_type,
    int8_t rssi,
    int64_t seen_us
)
{
    if (marker == NULL || seen_us < 0) {
        return;
    }
    if (marker->observation_kind ==
        AMULET_BLE_AMBIENT_OBSERVATION_DIAGNOSTIC) {
        record_marker_diagnostic(marker);
        return;
    }
    if (ambient_report_queue == NULL || ambient_free_slot_queue == NULL ||
        address == NULL || !logical_marker_is_valid(marker)) {
        return;
    }

    uint8_t slot;
    if (xQueueReceive(ambient_free_slot_queue, &slot, 0) != pdTRUE) {
        atomic_fetch_add(&ambient_report_drop_count, 1);
        return;
    }
    ambient_report_t *report = &ambient_report_pool[slot];
    *report = (ambient_report_t) {
        .marker = *marker,
        .seen_us = seen_us,
        .address_type = address_type,
        .rssi = rssi,
    };
    memcpy(report->address, address, sizeof(report->address));
    if (xQueueSend(ambient_report_queue, &slot, 0) != pdTRUE) {
        clear_sensitive(report, sizeof(*report));
        BaseType_t released = xQueueSend(ambient_free_slot_queue, &slot, 0);
        configASSERT(released == pdTRUE);
        (void)released;
        atomic_fetch_add(&ambient_report_drop_count, 1);
    }
}
