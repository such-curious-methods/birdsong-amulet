#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "amulet_ambient_observer.h"

static int failure_count;

#define CHECK(expression) \
    do { \
        if (!(expression)) { \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expression); \
            failure_count++; \
        } \
    } while (0)

static amulet_ambient_event_t make_event(
    uint8_t entity,
    uint8_t address,
    int64_t seen_us,
    int8_t rssi
)
{
    amulet_ambient_event_t event = {
        .seen_us = seen_us,
        .rule = AMULET_BLE_RULE_IBEACON,
        .category = AMULET_BLE_CATEGORY_LOCATION_INFRASTRUCTURE,
        .confidence = AMULET_BLE_CONFIDENCE_STRUCTURAL,
        .rssi = rssi,
        .rssi_valid = true,
    };
    memset(event.entity_key, entity, sizeof(event.entity_key));
    memset(event.address_key, address, sizeof(event.address_key));
    return event;
}

static void test_create_and_update(void)
{
    amulet_ambient_table_t table;
    amulet_ambient_table_init(&table);
    amulet_ambient_event_t first = make_event(1, 2, 100, -50);
    amulet_ambient_event_t second = make_event(1, 2, 200, -61);
    amulet_ambient_entry_t *entry = NULL;

    CHECK(amulet_ambient_record(&table, &first, 1000, &entry) ==
          AMULET_AMBIENT_RECORD_CREATED);
    CHECK(entry != NULL);
    CHECK(entry->first_seen_us == 100);
    CHECK(entry->last_seen_us == 100);
    CHECK(entry->report_count == 1);
    CHECK(!entry->address_changed);
    CHECK(amulet_ambient_record(&table, &second, 1000, &entry) ==
          AMULET_AMBIENT_RECORD_UPDATED);
    CHECK(entry->first_seen_us == 100);
    CHECK(entry->last_seen_us == 200);
    CHECK(entry->report_count == 2);
    CHECK(amulet_ambient_median_rssi(entry) == -56);
    CHECK(amulet_ambient_active_count(&table) == 1);
}

static void test_address_rotation(void)
{
    amulet_ambient_table_t table;
    amulet_ambient_table_init(&table);
    amulet_ambient_event_t first = make_event(3, 4, 100, -60);
    amulet_ambient_event_t rotated = make_event(3, 5, 200, -59);
    amulet_ambient_entry_t *entry = NULL;

    CHECK(amulet_ambient_record(&table, &first, 1000, NULL) ==
          AMULET_AMBIENT_RECORD_CREATED);
    CHECK(amulet_ambient_record(&table, &rotated, 1000, &entry) ==
          AMULET_AMBIENT_RECORD_UPDATED);
    CHECK(entry != NULL && entry->address_changed);
    CHECK(memcmp(
        entry->latest_address_key,
        rotated.address_key,
        sizeof(entry->latest_address_key)
    ) == 0);
}

static void test_expiry_creates_new_episode(void)
{
    amulet_ambient_table_t table;
    amulet_ambient_table_init(&table);
    amulet_ambient_event_t event = make_event(6, 7, 0, -70);

    CHECK(amulet_ambient_record(&table, &event, 500, NULL) ==
          AMULET_AMBIENT_RECORD_CREATED);
    CHECK(amulet_ambient_expire(&table, 499, 500) == 0);
    CHECK(amulet_ambient_expire(&table, 500, 500) == 1);
    CHECK(amulet_ambient_active_count(&table) == 0);
    event.seen_us = 501;
    CHECK(amulet_ambient_record(&table, &event, 500, NULL) ==
          AMULET_AMBIENT_RECORD_CREATED);
}

static void test_lru_replacement(void)
{
    amulet_ambient_table_t table;
    amulet_ambient_table_init(&table);
    for (size_t index = 0; index < AMULET_AMBIENT_TABLE_CAPACITY; index++) {
        amulet_ambient_event_t event = make_event(
            (uint8_t)index,
            (uint8_t)(index + 1),
            (int64_t)index,
            -50
        );
        CHECK(amulet_ambient_record(&table, &event, 10000, NULL) ==
              AMULET_AMBIENT_RECORD_CREATED);
    }

    amulet_ambient_event_t refreshed = make_event(0, 1, 1000, -50);
    CHECK(amulet_ambient_record(&table, &refreshed, 10000, NULL) ==
          AMULET_AMBIENT_RECORD_UPDATED);
    amulet_ambient_event_t replacement = make_event(100, 101, 2000, -50);
    CHECK(amulet_ambient_record(&table, &replacement, 10000, NULL) ==
          AMULET_AMBIENT_RECORD_REPLACED);
    CHECK(amulet_ambient_active_count(&table) == AMULET_AMBIENT_TABLE_CAPACITY);
    CHECK(amulet_ambient_find(
        &table,
        AMULET_BLE_RULE_IBEACON,
        refreshed.entity_key
    ) != NULL);
    amulet_ambient_event_t evicted = make_event(1, 2, 0, -50);
    CHECK(amulet_ambient_find(
        &table,
        AMULET_BLE_RULE_IBEACON,
        evicted.entity_key
    ) == NULL);
    CHECK(amulet_ambient_find(
        &table,
        AMULET_BLE_RULE_IBEACON,
        replacement.entity_key
    ) != NULL);
}

static void test_rssi_ring_and_unknown_value(void)
{
    amulet_ambient_table_t table;
    amulet_ambient_table_init(&table);
    static const int8_t samples[] = {-90, -80, -70, -60, -50, -40};
    amulet_ambient_entry_t *entry = NULL;
    for (size_t index = 0; index < sizeof(samples); index++) {
        amulet_ambient_event_t event = make_event(
            8,
            9,
            (int64_t)index,
            samples[index]
        );
        CHECK(amulet_ambient_record(&table, &event, 1000, &entry) !=
              AMULET_AMBIENT_RECORD_INVALID);
    }
    CHECK(entry != NULL && entry->rssi_sample_count == 5);
    CHECK(amulet_ambient_median_rssi(entry) == -60);

    amulet_ambient_event_t unknown = make_event(10, 11, 10, -12);
    unknown.rssi_valid = false;
    CHECK(amulet_ambient_record(&table, &unknown, 1000, &entry) ==
          AMULET_AMBIENT_RECORD_CREATED);
    CHECK(entry != NULL && entry->rssi_sample_count == 0);
    CHECK(amulet_ambient_median_rssi(entry) == AMULET_AMBIENT_RSSI_UNAVAILABLE);
}

static void test_saturating_count_and_invalid_event(void)
{
    amulet_ambient_table_t table;
    amulet_ambient_table_init(&table);
    amulet_ambient_event_t event = make_event(12, 13, 100, -50);
    amulet_ambient_entry_t *entry = NULL;
    CHECK(amulet_ambient_record(&table, &event, 1000, &entry) ==
          AMULET_AMBIENT_RECORD_CREATED);
    entry->report_count = UINT32_MAX;
    event.seen_us = 200;
    CHECK(amulet_ambient_record(&table, &event, 1000, &entry) ==
          AMULET_AMBIENT_RECORD_UPDATED);
    CHECK(entry->report_count == UINT32_MAX);

    event.rule = AMULET_BLE_RULE_NONE;
    entry = (amulet_ambient_entry_t *)(uintptr_t)1;
    CHECK(amulet_ambient_record(&table, &event, 1000, &entry) ==
          AMULET_AMBIENT_RECORD_INVALID);
    CHECK(entry == NULL);
}

int main(void)
{
    test_create_and_update();
    test_address_rotation();
    test_expiry_creates_new_episode();
    test_lru_replacement();
    test_rssi_ring_and_unknown_value();
    test_saturating_count_and_invalid_event();

    if (failure_count != 0) {
        fprintf(stderr, "%d ambient observer check(s) failed\n", failure_count);
        return 1;
    }

    puts("Ambient observer tests passed");
    return 0;
}
