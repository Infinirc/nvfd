#ifndef NVFD_FAN_H
#define NVFD_FAN_H

#include "nvfd.h"

int  fan_get_count(nvmlDevice_t device);
int  fan_get_speed(nvmlDevice_t device, unsigned int fan);
int  fan_set_speed(nvmlDevice_t device, unsigned int fan, unsigned int speed);
int  fan_set_gpu_speed(unsigned int gpu_index, unsigned int speed);
int  fan_set_all_speed(unsigned int speed);
int  fan_reset_to_auto(unsigned int gpu_index);
void fan_reset_all_to_auto(void);

#endif /* NVFD_FAN_H */
