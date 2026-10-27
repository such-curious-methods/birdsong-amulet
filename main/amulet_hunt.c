#include "amulet_hunt.h"

#include <string.h>

void amulet_hunt_init(amulet_hunt_t *hunt, int8_t near_rssi_dbm, int64_t rearm_us)
{
    memset(hunt, 0, sizeof(*hunt));
    hunt->near_rssi_dbm = near_rssi_dbm;
    hunt->rearm_us = rearm_us;
}

static amulet_hunt_beacon_t *find_beacon(amulet_hunt_t *hunt, uint8_t beacon)
{
    if (beacon == 0 || beacon > AMULET_HUNT_MAX_BEACONS) {
        return NULL;
    }
    return &hunt->beacons[beacon - 1U];
}

static int8_t median_rssi(const amulet_hunt_beacon_t *entry)
{
    int8_t sorted[AMULET_HUNT_RSSI_SAMPLES];
    memcpy(sorted, entry->rssi_samples, sizeof(sorted));
    for (size_t index = 1; index < AMULET_HUNT_RSSI_SAMPLES; index++) {
        int8_t value = sorted[index];
        size_t position = index;
        while (position > 0 && sorted[position - 1U] > value) {
            sorted[position] = sorted[position - 1U];
            position--;
        }
        sorted[position] = value;
    }
    return sorted[AMULET_HUNT_RSSI_SAMPLES / 2U];
}

bool amulet_hunt_report(amulet_hunt_t *hunt, uint8_t beacon, int8_t rssi, int64_t now_us)
{
    amulet_hunt_beacon_t *entry = find_beacon(hunt, beacon);
    if (entry == NULL || rssi == AMULET_HUNT_RSSI_UNKNOWN) {
        return false;
    }

    // Stale samples from an earlier visit never count toward this one.
    if (entry->sample_count > 0 &&
        now_us - entry->last_sample_us > AMULET_HUNT_SAMPLE_WINDOW_US) {
        entry->sample_count = 0;
        entry->next_sample = 0;
    }
    entry->rssi_samples[entry->next_sample] = rssi;
    entry->next_sample = (uint8_t)((entry->next_sample + 1U) % AMULET_HUNT_RSSI_SAMPLES);
    if (entry->sample_count < AMULET_HUNT_RSSI_SAMPLES) {
        entry->sample_count++;
    }
    entry->last_sample_us = now_us;

    bool near = entry->sample_count == AMULET_HUNT_RSSI_SAMPLES &&
                median_rssi(entry) >= hunt->near_rssi_dbm;
    if (!near) {
        return false;
    }

    if (entry->state == AMULET_HUNT_CELEBRATED && entry->near_seen &&
        now_us - entry->last_near_us >= hunt->rearm_us) {
        entry->state = AMULET_HUNT_ARMED;
    }
    entry->last_near_us = now_us;
    entry->near_seen = true;
    if (entry->state != AMULET_HUNT_ARMED) {
        return false;
    }
    entry->state = AMULET_HUNT_PENDING;
    return true;
}

void amulet_hunt_celebrated(amulet_hunt_t *hunt, uint8_t beacon)
{
    amulet_hunt_beacon_t *entry = find_beacon(hunt, beacon);
    if (entry != NULL && entry->state == AMULET_HUNT_PENDING) {
        entry->state = AMULET_HUNT_CELEBRATED;
    }
}

void amulet_hunt_cancel(amulet_hunt_t *hunt, uint8_t beacon)
{
    amulet_hunt_beacon_t *entry = find_beacon(hunt, beacon);
    if (entry != NULL && entry->state == AMULET_HUNT_PENDING) {
        entry->state = AMULET_HUNT_ARMED;
    }
}

static uint8_t scale_channel(uint32_t value, uint8_t level)
{
    return (uint8_t)((value * level + 127U) / 255U);
}

void amulet_hunt_rainbow_rgb(
    uint32_t hue,
    uint8_t level,
    uint8_t *red,
    uint8_t *green,
    uint8_t *blue
)
{
    hue %= AMULET_HUNT_HUE_STEPS;
    uint32_t rising = hue % 256U;
    uint32_t falling = 255U - rising;
    uint32_t r = 0;
    uint32_t g = 0;
    uint32_t b = 0;
    switch (hue / 256U) {
    case 0:
        r = 255U;
        g = rising;
        break;
    case 1:
        r = falling;
        g = 255U;
        break;
    case 2:
        g = 255U;
        b = rising;
        break;
    case 3:
        g = falling;
        b = 255U;
        break;
    case 4:
        r = rising;
        b = 255U;
        break;
    default:
        r = 255U;
        b = falling;
        break;
    }
    *red = scale_channel(r, level);
    *green = scale_channel(g, level);
    *blue = scale_channel(b, level);
}
