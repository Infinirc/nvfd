#ifndef NVFD_FAN_H
#define NVFD_FAN_H

#include "nvfd.h"

/* Speeds sent to the hardware are clamped to this range. */
#define FAN_SPEED_MIN 30
#define FAN_SPEED_MAX 100

int  fan_get_count(nvmlDevice_t device);
int  fan_get_speed(nvmlDevice_t device, unsigned int fan);
int  fan_set_speed(nvmlDevice_t device, unsigned int fan, unsigned int speed);
int  fan_set_gpu_speed(unsigned int gpu_index, unsigned int speed);
int  fan_set_all_speed(unsigned int speed);
int  fan_reset_to_auto(unsigned int gpu_index);
/* Returns the number of GPUs that could not be reset. */
int  fan_reset_all_to_auto(void);

#endif /* NVFD_FAN_H */
