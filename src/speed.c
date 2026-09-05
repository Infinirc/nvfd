#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "speed.h"

int fan_speed_clamp(long long speed, int *adjusted) {
    int result;

    if (speed < FAN_SPEED_MIN)
        result = FAN_SPEED_MIN;
    else if (speed > FAN_SPEED_MAX)
        result = FAN_SPEED_MAX;
    else
        result = (int)speed;

    if (adjusted)
        *adjusted = speed != result;
    return result;
}

int fan_failsafe_speed(int temp, int limit, int fan_speed, int *tripped) {
    if (limit < 0 || temp < 0)
        return fan_speed;

    if (temp >= limit) {
        *tripped = 1;
        return FAN_SPEED_MAX;
    }
    if (*tripped) {
        if (temp > limit - FAN_FAILSAFE_RELEASE_C)
            return FAN_SPEED_MAX;
        *tripped = 0;
    }
    return fan_speed;
}

LegacyConfig legacy_config_parse(const char *value) {
    LegacyConfig config = {LEGACY_MODE_AUTO, 0, 0};

    if (strcmp(value, "auto") == 0)
        return config;
    if (strcmp(value, "curve") == 0) {
        config.mode = LEGACY_MODE_CURVE;
        return config;
    }

    char *end;
    errno = 0;
    long speed = strtol(value, &end, 10);
    if (end == value) {
        config.adjusted = 1;
        return config;
    }

    config.mode = LEGACY_MODE_MANUAL;
    config.speed = fan_speed_clamp(speed, &config.adjusted);
    if (errno == ERANGE || *end != '\0')
        config.adjusted = 1;
    return config;
}
