#ifndef AMULET_HUNT_H
#define AMULET_HUNT_H

#include <stdbool.h>
#include <stdint.h>

// Cozy scavenger hunt: silent one-way beacons that an Amulet celebrates with a
// rainbow. The Amulet never transmits anything for the hunt, and this state is
// RAM-only, so nothing records who found which beacon.
#define AMULET_HUNT_MAX_BEACONS 8U
// A beacon counts as near when the median of this many recent sightings, all
// within the sample window, reaches the near threshold.
#define AMULET_HUNT_RSSI_SAMPLES 3U
#define AMULET_HUNT_SAMPLE_WINDOW_US (5LL * 1000LL * 1000LL)
// NimBLE reports 127 when the controller has no RSSI for an advertisement.
#define AMULET_HUNT_RSSI_UNKNOWN 127
// Six color-wheel sectors of 256 steps each.
#define AMULET_HUNT_HUE_STEPS 1536U

typedef enum {
    AMULET_HUNT_ARMED,
    AMULET_HUNT_PENDING,
    AMULET_HUNT_CELEBRATED,
} amulet_hunt_beacon_state_t;

typedef struct {
    amulet_hunt_beacon_state_t state;
    int8_t rssi_samples[AMULET_HUNT_RSSI_SAMPLES];
    uint8_t sample_count;
    uint8_t next_sample;
    int64_t last_sample_us;
    int64_t last_near_us;
    bool near_seen;
} amulet_hunt_beacon_t;

typedef struct {
    amulet_hunt_beacon_t beacons[AMULET_HUNT_MAX_BEACONS];
    int8_t near_rssi_dbm;
    int64_t rearm_us;
} amulet_hunt_t;

void amulet_hunt_init(amulet_hunt_t *hunt, int8_t near_rssi_dbm, int64_t rearm_us);

// Records one sighting of beacon 1..AMULET_HUNT_MAX_BEACONS. Returns true when
// the beacon has just been found; it is then pending until celebrated or
// cancelled. A celebrated beacon re-arms once it has not been near for the
// re-arm period, so coming back later earns another rainbow.
bool amulet_hunt_report(amulet_hunt_t *hunt, uint8_t beacon, int8_t rssi, int64_t now_us);

// The rainbow for a pending beacon played to the end.
void amulet_hunt_celebrated(amulet_hunt_t *hunt, uint8_t beacon);

// A pending celebration could not be queued; a later sighting may try again.
void amulet_hunt_cancel(amulet_hunt_t *hunt, uint8_t beacon);

// Maps a hue in 0..AMULET_HUNT_HUE_STEPS-1 (red, yellow, green, cyan, blue,
// magenta) to an RGB color whose brightest channel is level.
void amulet_hunt_rainbow_rgb(
    uint32_t hue,
    uint8_t level,
    uint8_t *red,
    uint8_t *green,
    uint8_t *blue
);

#endif
