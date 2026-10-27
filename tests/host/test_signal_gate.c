#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "amulet_signal_gate.h"

static int failure_count;

#define CHECK(expression) \
    do { \
        if (!(expression)) { \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expression); \
            failure_count++; \
        } \
    } while (0)

#define SECONDS(value) ((int64_t)(value) * 1000LL * 1000LL)

static void test_channel_burst_counts_once(void)
{
    amulet_signal_gate_t gate;
    amulet_signal_gate_init(&gate);

    // One broadcast heard on three advertising channels within milliseconds.
    CHECK(!amulet_signal_gate_report(&gate, 0));
    CHECK(!amulet_signal_gate_report(&gate, 2000));
    CHECK(!amulet_signal_gate_report(&gate, 4000));
    // Many more reports inside the first second still count as one sighting.
    for (int64_t time_us = 10000; time_us < SECONDS(1); time_us += 100000) {
        CHECK(!amulet_signal_gate_report(&gate, time_us));
    }
}

static void test_three_spaced_sightings_indicate(void)
{
    amulet_signal_gate_t gate;
    amulet_signal_gate_init(&gate);

    CHECK(!amulet_signal_gate_report(&gate, SECONDS(0)));
    CHECK(!amulet_signal_gate_report(&gate, SECONDS(2)));
    CHECK(amulet_signal_gate_report(&gate, SECONDS(4)));
}

static void test_sightings_outside_window_do_not_indicate(void)
{
    amulet_signal_gate_t gate;
    amulet_signal_gate_init(&gate);

    CHECK(!amulet_signal_gate_report(&gate, SECONDS(0)));
    CHECK(!amulet_signal_gate_report(&gate, SECONDS(10)));
    CHECK(!amulet_signal_gate_report(&gate, SECONDS(20)));
    // The last three now span 10 s to 22 s: inside the window.
    CHECK(amulet_signal_gate_report(&gate, SECONDS(22)));
}

static void test_holdoff_limits_repeats(void)
{
    amulet_signal_gate_t gate;
    amulet_signal_gate_init(&gate);

    CHECK(!amulet_signal_gate_report(&gate, SECONDS(0)));
    CHECK(!amulet_signal_gate_report(&gate, SECONDS(2)));
    CHECK(amulet_signal_gate_report(&gate, SECONDS(4)));

    // A tracker that keeps broadcasting every two seconds stays quiet until
    // the hold-off has passed, then indicates once more.
    int indications = 0;
    for (int64_t time_us = SECONDS(6); time_us < SECONDS(64); time_us += SECONDS(2)) {
        if (amulet_signal_gate_report(&gate, time_us)) {
            indications++;
            CHECK(time_us >= SECONDS(64));
        }
    }
    CHECK(indications == 0);
    CHECK(amulet_signal_gate_report(&gate, SECONDS(64)));
}

int main(void)
{
    test_channel_burst_counts_once();
    test_three_spaced_sightings_indicate();
    test_sightings_outside_window_do_not_indicate();
    test_holdoff_limits_repeats();

    if (failure_count != 0) {
        fprintf(stderr, "%d signal gate check(s) failed\n", failure_count);
        return 1;
    }

    puts("Signal gate checks passed");
    return 0;
}
