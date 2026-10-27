#ifndef AMULET_AMBIENT_OBSERVER_H
#define AMULET_AMBIENT_OBSERVER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "amulet_ble_classifier.h"

#define AMULET_AMBIENT_KEY_LENGTH 16U
#define AMULET_AMBIENT_TABLE_CAPACITY 64U
#define AMULET_AMBIENT_RSSI_SAMPLE_COUNT 5U
#define AMULET_AMBIENT_RSSI_UNAVAILABLE 127

typedef struct {
    uint8_t entity_key[AMULET_AMBIENT_KEY_LENGTH];
    uint8_t address_key[AMULET_AMBIENT_KEY_LENGTH];
    int64_t seen_us;
    amulet_ble_rule_t rule;
    amulet_ble_category_t category;
    amulet_ble_confidence_t confidence;
    int8_t rssi;
    bool rssi_valid;
} amulet_ambient_event_t;

typedef struct {
    uint8_t entity_key[AMULET_AMBIENT_KEY_LENGTH];
    uint8_t latest_address_key[AMULET_AMBIENT_KEY_LENGTH];
    int64_t first_seen_us;
    int64_t last_seen_us;
    uint32_t report_count;
    amulet_ble_rule_t rule;
    amulet_ble_category_t category;
    amulet_ble_confidence_t confidence;
    int8_t rssi_samples[AMULET_AMBIENT_RSSI_SAMPLE_COUNT];
    uint8_t rssi_sample_count;
    uint8_t next_rssi_index;
    bool address_changed;
    bool active;
} amulet_ambient_entry_t;

typedef struct {
    amulet_ambient_entry_t entries[AMULET_AMBIENT_TABLE_CAPACITY];
} amulet_ambient_table_t;

typedef enum {
    AMULET_AMBIENT_RECORD_INVALID,
    AMULET_AMBIENT_RECORD_CREATED,
    AMULET_AMBIENT_RECORD_UPDATED,
    AMULET_AMBIENT_RECORD_REPLACED,
} amulet_ambient_record_result_t;

void amulet_ambient_table_init(amulet_ambient_table_t *table);

amulet_ambient_record_result_t amulet_ambient_record(
    amulet_ambient_table_t *table,
    const amulet_ambient_event_t *event,
    int64_t expiry_us,
    amulet_ambient_entry_t **entry_out
);

size_t amulet_ambient_expire(
    amulet_ambient_table_t *table,
    int64_t now_us,
    int64_t expiry_us
);

size_t amulet_ambient_active_count(const amulet_ambient_table_t *table);

const amulet_ambient_entry_t *amulet_ambient_find(
    const amulet_ambient_table_t *table,
    amulet_ble_rule_t rule,
    const uint8_t entity_key[AMULET_AMBIENT_KEY_LENGTH]
);

int8_t amulet_ambient_median_rssi(const amulet_ambient_entry_t *entry);

#endif
