#ifndef AMULET_SIGNAL_GATE_H
#define AMULET_SIGNAL_GATE_H

#include <stdbool.h>
#include <stdint.h>

// Persistence gate for provisional signature indications (roadmap Phase 4
// starting policy): a signal must be seen on several separate occasions
// within a short window, and is indicated at most once per hold-off.
#define AMULET_SIGNAL_GATE_REQUIRED_SIGHTINGS 3U
#define AMULET_SIGNAL_GATE_WINDOW_US (15LL * 1000LL * 1000LL)
#define AMULET_SIGNAL_GATE_HOLDOFF_US (60LL * 1000LL * 1000LL)
// A scanner without duplicate filtering reports one broadcast several times
// (once per advertising channel), so closer reports count as one sighting.
#define AMULET_SIGNAL_GATE_MIN_SPACING_US (1000LL * 1000LL)

typedef struct {
    int64_t sightings_us[AMULET_SIGNAL_GATE_REQUIRED_SIGHTINGS];
    uint8_t sighting_count;
    uint8_t next_sighting;
    int64_t last_indicated_us;
    bool indicated;
} amulet_signal_gate_t;

void amulet_signal_gate_init(amulet_signal_gate_t *gate);

// Records a report at now_us and returns true when it should be indicated.
bool amulet_signal_gate_report(amulet_signal_gate_t *gate, int64_t now_us);

#endif
