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

int fan_speed_clamp(long long speed, int *adjusted);
LegacyConfig legacy_config_parse(const char *value);

#endif /* NVFD_SPEED_H */
