#ifndef NVFD_H
#define NVFD_H

#include <nvml.h>
#include <signal.h>

#define NVFD_VERSION "1.0"

#define NVFD_CONFIG_DIR   "/etc/nvfd"
#define NVFD_CONFIG_FILE  "/etc/nvfd/config.json"
#define NVFD_CURVE_FILE   "/etc/nvfd/curve.json"

/* Legacy paths for migration */
#define NVFD_OLD_CONFIG_FILE "/etc/infinirc_gpu_fan_control.conf"
#define NVFD_OLD_CURVE_FILE  "/etc/infinirc_gpu_fan_curve.json"

#define MAX_GPU_COUNT    8
#define MAX_FAN_COUNT    4
#define MAX_CURVE_POINTS 20

typedef struct {
    int temperature;
    int fan_speed;
} FanCurvePoint;

typedef struct {
    FanCurvePoint points[MAX_CURVE_POINTS];
    int point_count;
} FanCurve;

extern unsigned int device_count;
extern volatile sig_atomic_t keep_running;
extern volatile sig_atomic_t reload_config;

#endif /* NVFD_H */
