#include "amulet_signal_gate.h"

#include <string.h>

void amulet_signal_gate_init(amulet_signal_gate_t *gate)
{
    memset(gate, 0, sizeof(*gate));
}

static int64_t newest_sighting(const amulet_signal_gate_t *gate)
{
    uint8_t newest = (uint8_t)((gate->next_sighting +
        AMULET_SIGNAL_GATE_REQUIRED_SIGHTINGS - 1U) %
        AMULET_SIGNAL_GATE_REQUIRED_SIGHTINGS);
    return gate->sightings_us[newest];
}

bool amulet_signal_gate_report(amulet_signal_gate_t *gate, int64_t now_us)
{
    if (gate->sighting_count > 0 &&
        now_us - newest_sighting(gate) < AMULET_SIGNAL_GATE_MIN_SPACING_US) {
        return false;
    }

    gate->sightings_us[gate->next_sighting] = now_us;
    gate->next_sighting = (uint8_t)((gate->next_sighting + 1U) %
        AMULET_SIGNAL_GATE_REQUIRED_SIGHTINGS);
    if (gate->sighting_count < AMULET_SIGNAL_GATE_REQUIRED_SIGHTINGS) {
        gate->sighting_count++;
    }
    if (gate->sighting_count < AMULET_SIGNAL_GATE_REQUIRED_SIGHTINGS) {
        return false;
    }

    // After a full ring, next_sighting points at the oldest of the last three.
    int64_t oldest_us = gate->sightings_us[gate->next_sighting];
    if (now_us - oldest_us > AMULET_SIGNAL_GATE_WINDOW_US) {
        return false;
    }
    if (gate->indicated &&
        now_us - gate->last_indicated_us < AMULET_SIGNAL_GATE_HOLDOFF_US) {
        return false;
    }

    gate->indicated = true;
    gate->last_indicated_us = now_us;
    return true;
}
