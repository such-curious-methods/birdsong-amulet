#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "amulet_hunt.h"

static int failure_count;

#define CHECK(expression) \
    do { \
        if (!(expression)) { \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expression); \
            failure_count++; \
        } \
    } while (0)

#define SECOND_US (1000LL * 1000LL)
#define MINUTE_US (60LL * SECOND_US)
#define NEAR_DBM (-65)
#define REARM_US (5LL * MINUTE_US)

// Reports three sightings 200 ms apart starting at start_us and returns
// whether any of them found the beacon.
static bool visit(amulet_hunt_t *hunt, uint8_t beacon, int8_t rssi, int64_t start_us)
{
    bool found = false;
    for (int index = 0; index < 3; index++) {
        if (amulet_hunt_report(hunt, beacon, rssi, start_us + index * 200000LL)) {
            found = true;
        }
    }
    return found;
}

static void test_found_needs_three_near_sightings(void)
{
    amulet_hunt_t hunt;
    amulet_hunt_init(&hunt, NEAR_DBM, REARM_US);

    CHECK(!amulet_hunt_report(&hunt, 1, -50, 0));
    CHECK(!amulet_hunt_report(&hunt, 1, -50, 200000));
    CHECK(amulet_hunt_report(&hunt, 1, -50, 400000));
    // Pending: further sightings do not queue a second celebration.
    CHECK(!amulet_hunt_report(&hunt, 1, -50, 600000));
}

static void test_median_ignores_one_strong_outlier(void)
{
    amulet_hunt_t hunt;
    amulet_hunt_init(&hunt, NEAR_DBM, REARM_US);

    CHECK(!amulet_hunt_report(&hunt, 1, -80, 0));
    CHECK(!amulet_hunt_report(&hunt, 1, -40, 200000));
    CHECK(!amulet_hunt_report(&hunt, 1, -80, 400000));
    // Two strong of three is enough.
    CHECK(amulet_hunt_report(&hunt, 1, -40, 600000));
}

static void test_far_unknown_and_invalid_never_count(void)
{
    amulet_hunt_t hunt;
    amulet_hunt_init(&hunt, NEAR_DBM, REARM_US);

    CHECK(!visit(&hunt, 1, -66, 0));
    CHECK(!visit(&hunt, 2, AMULET_HUNT_RSSI_UNKNOWN, 0));
    CHECK(!visit(&hunt, 0, -40, 0));
    CHECK(!visit(&hunt, AMULET_HUNT_MAX_BEACONS + 1, -40, 0));
    // Exactly at the threshold counts.
    CHECK(visit(&hunt, 3, NEAR_DBM, 0));
}

static void test_stale_samples_reset(void)
{
    amulet_hunt_t hunt;
    amulet_hunt_init(&hunt, NEAR_DBM, REARM_US);

    CHECK(!amulet_hunt_report(&hunt, 1, -50, 0));
    CHECK(!amulet_hunt_report(&hunt, 1, -50, 200000));
    // Longer than the sample window later, the earlier two no longer count.
    int64_t later_us = 200000 + AMULET_HUNT_SAMPLE_WINDOW_US + 1;
    CHECK(!amulet_hunt_report(&hunt, 1, -50, later_us));
    CHECK(!amulet_hunt_report(&hunt, 1, -50, later_us + 200000));
    CHECK(amulet_hunt_report(&hunt, 1, -50, later_us + 400000));
}

static void test_celebrated_rearms_only_after_time_away(void)
{
    amulet_hunt_t hunt;
    amulet_hunt_init(&hunt, NEAR_DBM, REARM_US);

    CHECK(visit(&hunt, 1, -50, 0));
    amulet_hunt_celebrated(&hunt, 1);

    // Staying near keeps it quiet, however long that lasts.
    for (int64_t now_us = 10 * SECOND_US; now_us < 20 * MINUTE_US; now_us += 30 * SECOND_US) {
        CHECK(!visit(&hunt, 1, -50, now_us));
    }

    // Weak sightings while away do not count as near and do not re-arm.
    int64_t left_us = 20 * MINUTE_US;
    CHECK(!visit(&hunt, 1, -90, left_us + MINUTE_US));
    // Back too soon.
    CHECK(!visit(&hunt, 1, -50, left_us + 4 * MINUTE_US));
    // Away long enough after that visit: another rainbow.
    CHECK(visit(&hunt, 1, -50, left_us + 4 * MINUTE_US + REARM_US));
}

static void test_cancel_allows_retry_and_beacons_are_independent(void)
{
    amulet_hunt_t hunt;
    amulet_hunt_init(&hunt, NEAR_DBM, REARM_US);

    CHECK(visit(&hunt, 1, -50, 0));
    amulet_hunt_cancel(&hunt, 1);
    CHECK(amulet_hunt_report(&hunt, 1, -50, 800000));

    // Beacon 2 is found while beacon 1 is still pending.
    CHECK(visit(&hunt, 2, -50, 0));
    // Celebrating an armed or unknown beacon changes nothing.
    amulet_hunt_celebrated(&hunt, 3);
    CHECK(visit(&hunt, 3, -50, 0));
}

static void test_rainbow_colors(void)
{
    uint8_t red;
    uint8_t green;
    uint8_t blue;

    amulet_hunt_rainbow_rgb(0, 255, &red, &green, &blue);
    CHECK(red == 255 && green == 0 && blue == 0);
    amulet_hunt_rainbow_rgb(256, 255, &red, &green, &blue);
    CHECK(red == 255 && green == 255 && blue == 0);
    amulet_hunt_rainbow_rgb(512, 255, &red, &green, &blue);
    CHECK(red == 0 && green == 255 && blue == 0);
    amulet_hunt_rainbow_rgb(768, 255, &red, &green, &blue);
    CHECK(red == 0 && green == 255 && blue == 255);
    amulet_hunt_rainbow_rgb(1024, 255, &red, &green, &blue);
    CHECK(red == 0 && green == 0 && blue == 255);
    amulet_hunt_rainbow_rgb(1280, 255, &red, &green, &blue);
    CHECK(red == 255 && green == 0 && blue == 255);
    // Wraps around, and the brightest channel equals the level.
    amulet_hunt_rainbow_rgb(AMULET_HUNT_HUE_STEPS, 40, &red, &green, &blue);
    CHECK(red == 40 && green == 0 && blue == 0);
    for (uint32_t hue = 0; hue < AMULET_HUNT_HUE_STEPS; hue++) {
        amulet_hunt_rainbow_rgb(hue, 40, &red, &green, &blue);
        uint8_t brightest = red > green ? red : green;
        brightest = blue > brightest ? blue : brightest;
        CHECK(brightest == 40);
    }
}

int main(void)
{
    test_found_needs_three_near_sightings();
    test_median_ignores_one_strong_outlier();
    test_far_unknown_and_invalid_never_count();
    test_stale_samples_reset();
    test_celebrated_rearms_only_after_time_away();
    test_cancel_allows_retry_and_beacons_are_independent();
    test_rainbow_colors();

    if (failure_count != 0) {
        fprintf(stderr, "%d hunt check(s) failed\n", failure_count);
        return 1;
    }

    puts("Hunt state checks passed");
    return 0;
}
