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
