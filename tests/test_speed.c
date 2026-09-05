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
    test_legacy_values();
    puts("speed tests passed");
    return 0;
}
