#ifndef NVFD_SPEED_H
#define NVFD_SPEED_H

/* Speeds sent to the hardware are clamped to this range. */
#define FAN_SPEED_MIN 30
#define FAN_SPEED_MAX 100

typedef enum {
    LEGACY_MODE_AUTO,
    LEGACY_MODE_CURVE,
    LEGACY_MODE_MANUAL
} LegacyMode;

typedef struct {
    LegacyMode mode;
    int speed;
    int adjusted;
} LegacyConfig;

/* Degrees below the trip point at which a tripped failsafe releases again. */
#define FAN_FAILSAFE_RELEASE_C 5

int fan_speed_clamp(long long speed, int *adjusted);

/*
 * Raises - never lowers - a computed fan speed once the die reaches the card's
 * own throttle point. *tripped carries the latch between calls so the override
 * releases with hysteresis instead of chattering at the boundary.
 * limit < 0 (no usable threshold) or temp < 0 (no reading) returns fan_speed.
 */
int fan_failsafe_speed(int temp, int limit, int fan_speed, int *tripped);
LegacyConfig legacy_config_parse(const char *value);

#endif /* NVFD_SPEED_H */
