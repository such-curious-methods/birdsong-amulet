#include "amulet_ambient_observer.h"

#include <limits.h>
#include <string.h>

static bool event_is_valid(const amulet_ambient_event_t *event)
{
    return event != NULL &&
           event->seen_us >= 0 &&
           event->rule != AMULET_BLE_RULE_NONE &&
           event->category != AMULET_BLE_CATEGORY_NONE &&
           event->confidence != AMULET_BLE_CONFIDENCE_NONE;
}

static bool entry_matches(
    const amulet_ambient_entry_t *entry,
    const amulet_ambient_event_t *event
)
{
    return entry->active &&
           entry->rule == event->rule &&
           memcmp(
               entry->entity_key,
               event->entity_key,
               sizeof(entry->entity_key)
           ) == 0;
}

static void add_rssi_sample(
    amulet_ambient_entry_t *entry,
    int8_t rssi,
    bool valid
)
{
    if (!valid) {
        return;
    }

    entry->rssi_samples[entry->next_rssi_index] = rssi;
    entry->next_rssi_index =
        (entry->next_rssi_index + 1) % AMULET_AMBIENT_RSSI_SAMPLE_COUNT;
    if (entry->rssi_sample_count < AMULET_AMBIENT_RSSI_SAMPLE_COUNT) {
        entry->rssi_sample_count++;
    }
}

static void initialize_entry(
    amulet_ambient_entry_t *entry,
    const amulet_ambient_event_t *event
)
{
    memset(entry, 0, sizeof(*entry));
    memcpy(entry->entity_key, event->entity_key, sizeof(entry->entity_key));
    memcpy(
        entry->latest_address_key,
        event->address_key,
        sizeof(entry->latest_address_key)
    );
    entry->first_seen_us = event->seen_us;
    entry->last_seen_us = event->seen_us;
    entry->report_count = 1;
    entry->rule = event->rule;
    entry->category = event->category;
    entry->confidence = event->confidence;
    entry->active = true;
    add_rssi_sample(entry, event->rssi, event->rssi_valid);
}

void amulet_ambient_table_init(amulet_ambient_table_t *table)
{
    if (table != NULL) {
        memset(table, 0, sizeof(*table));
    }
}

size_t amulet_ambient_expire(
    amulet_ambient_table_t *table,
    int64_t now_us,
    int64_t expiry_us
)
{
    if (table == NULL || now_us < 0 || expiry_us <= 0) {
        return 0;
    }

    size_t expired = 0;
    for (size_t index = 0; index < AMULET_AMBIENT_TABLE_CAPACITY; index++) {
        amulet_ambient_entry_t *entry = &table->entries[index];
        if (entry->active && now_us >= entry->last_seen_us &&
            now_us - entry->last_seen_us >= expiry_us) {
            memset(entry, 0, sizeof(*entry));
            expired++;
        }
    }
    return expired;
}

amulet_ambient_record_result_t amulet_ambient_record(
    amulet_ambient_table_t *table,
    const amulet_ambient_event_t *event,
    int64_t expiry_us,
    amulet_ambient_entry_t **entry_out
)
{
    if (entry_out != NULL) {
        *entry_out = NULL;
    }
    if (table == NULL || !event_is_valid(event) || expiry_us <= 0) {
        return AMULET_AMBIENT_RECORD_INVALID;
    }

    amulet_ambient_expire(table, event->seen_us, expiry_us);

    amulet_ambient_entry_t *available = NULL;
    amulet_ambient_entry_t *oldest = NULL;
    for (size_t index = 0; index < AMULET_AMBIENT_TABLE_CAPACITY; index++) {
        amulet_ambient_entry_t *entry = &table->entries[index];
        if (entry_matches(entry, event)) {
            if (memcmp(
                    entry->latest_address_key,
                    event->address_key,
                    sizeof(entry->latest_address_key)
                ) != 0) {
                entry->address_changed = true;
                memcpy(
                    entry->latest_address_key,
                    event->address_key,
                    sizeof(entry->latest_address_key)
                );
            }
            if (entry->report_count < UINT32_MAX) {
                entry->report_count++;
            }
            if (event->seen_us > entry->last_seen_us) {
                entry->last_seen_us = event->seen_us;
            }
            entry->category = event->category;
            entry->confidence = event->confidence;
            add_rssi_sample(entry, event->rssi, event->rssi_valid);
            if (entry_out != NULL) {
                *entry_out = entry;
            }
            return AMULET_AMBIENT_RECORD_UPDATED;
        }
        if (!entry->active && available == NULL) {
            available = entry;
        }
        if (entry->active &&
            (oldest == NULL || entry->last_seen_us < oldest->last_seen_us)) {
            oldest = entry;
        }
    }

    amulet_ambient_record_result_t result = AMULET_AMBIENT_RECORD_CREATED;
    if (available == NULL) {
        available = oldest;
        result = AMULET_AMBIENT_RECORD_REPLACED;
    }
    initialize_entry(available, event);
    if (entry_out != NULL) {
        *entry_out = available;
    }
    return result;
}

size_t amulet_ambient_active_count(const amulet_ambient_table_t *table)
{
    if (table == NULL) {
        return 0;
    }

    size_t count = 0;
    for (size_t index = 0; index < AMULET_AMBIENT_TABLE_CAPACITY; index++) {
        count += table->entries[index].active ? 1U : 0U;
    }
    return count;
}

const amulet_ambient_entry_t *amulet_ambient_find(
    const amulet_ambient_table_t *table,
    amulet_ble_rule_t rule,
    const uint8_t entity_key[AMULET_AMBIENT_KEY_LENGTH]
)
{
    if (table == NULL || entity_key == NULL || rule == AMULET_BLE_RULE_NONE) {
        return NULL;
    }

    for (size_t index = 0; index < AMULET_AMBIENT_TABLE_CAPACITY; index++) {
        const amulet_ambient_entry_t *entry = &table->entries[index];
        if (entry->active && entry->rule == rule &&
            memcmp(entry->entity_key, entity_key, sizeof(entry->entity_key)) == 0) {
            return entry;
        }
    }
    return NULL;
}

int8_t amulet_ambient_median_rssi(const amulet_ambient_entry_t *entry)
{
    if (entry == NULL || entry->rssi_sample_count == 0) {
        return AMULET_AMBIENT_RSSI_UNAVAILABLE;
    }

    int8_t sorted[AMULET_AMBIENT_RSSI_SAMPLE_COUNT];
    memcpy(sorted, entry->rssi_samples, entry->rssi_sample_count);
    for (uint8_t index = 1; index < entry->rssi_sample_count; index++) {
        int8_t sample = sorted[index];
        uint8_t position = index;
        while (position > 0 && sorted[position - 1] > sample) {
            sorted[position] = sorted[position - 1];
            position--;
        }
        sorted[position] = sample;
    }

    uint8_t middle = entry->rssi_sample_count / 2;
    if (entry->rssi_sample_count % 2 != 0) {
        return sorted[middle];
    }
    int16_t sum = (int16_t)sorted[middle - 1] + sorted[middle];
    if (sum < 0 && sum % 2 != 0) {
        sum--;
    }
    return (int8_t)(sum / 2);
}
