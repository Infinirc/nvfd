#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "speed.h"

static void test_clamp(void) {
    int adjusted = 0;

    assert(fan_speed_clamp(20, &adjusted) == 30);
    assert(adjusted == 1);
    assert(fan_speed_clamp(30, &adjusted) == 30);
    assert(adjusted == 0);
    assert(fan_speed_clamp(100, &adjusted) == 100);
    assert(adjusted == 0);
    assert(fan_speed_clamp(101, &adjusted) == 100);
    assert(adjusted == 1);
}

static void test_failsafe(void) {
    int tripped = 0;

    /* No usable threshold, or no reading: the computed speed stands. */
    assert(fan_failsafe_speed(95, -1, 40, &tripped) == 40);
    assert(tripped == 0);
    assert(fan_failsafe_speed(-1, 87, 40, &tripped) == 40);
    assert(tripped == 0);

    /* Below the trip point nothing changes. */
    assert(fan_failsafe_speed(86, 87, 40, &tripped) == 40);
    assert(tripped == 0);

    /* At and above it the fans go flat out, whatever the mode asked for. */
    assert(fan_failsafe_speed(87, 87, 40, &tripped) == FAN_SPEED_MAX);
    assert(tripped == 1);
    assert(fan_failsafe_speed(99, 87, 30, &tripped) == FAN_SPEED_MAX);

    /* Hysteresis: it holds until FAN_FAILSAFE_RELEASE_C below the trip point. */
    assert(fan_failsafe_speed(86, 87, 40, &tripped) == FAN_SPEED_MAX);
    assert(tripped == 1);
    assert(fan_failsafe_speed(83, 87, 40, &tripped) == FAN_SPEED_MAX);
    assert(tripped == 1);
    assert(fan_failsafe_speed(82, 87, 40, &tripped) == 40);
    assert(tripped == 0);

    /* It only ever raises: a curve already above the floor is left alone. */
    tripped = 0;
    assert(fan_failsafe_speed(95, 87, FAN_SPEED_MAX, &tripped) == FAN_SPEED_MAX);
}

static void test_legacy_values(void) {
    LegacyConfig config;

    config = legacy_config_parse("auto");
    assert(config.mode == LEGACY_MODE_AUTO);
    assert(config.adjusted == 0);

    config = legacy_config_parse("curve");
    assert(config.mode == LEGACY_MODE_CURVE);
    assert(config.adjusted == 0);

    config = legacy_config_parse("20");
    assert(config.mode == LEGACY_MODE_MANUAL);
    assert(config.speed == 30);
    assert(config.adjusted == 1);

    config = legacy_config_parse("60%");
    assert(config.mode == LEGACY_MODE_MANUAL);
    assert(config.speed == 60);
    assert(config.adjusted == 1);

    config = legacy_config_parse("abc");
    assert(config.mode == LEGACY_MODE_AUTO);
    assert(config.adjusted == 1);
}

int main(void) {
    test_clamp();
    test_failsafe();
    test_legacy_values();
    puts("speed tests passed");
    return 0;
}
