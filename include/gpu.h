#ifndef NVFD_GPU_H
#define NVFD_GPU_H

#include "nvfd.h"

int  gpu_init(void);
void gpu_shutdown(void);
int  gpu_get_handle(unsigned int index, nvmlDevice_t *device);
int  gpu_get_temperature(nvmlDevice_t device);
int  gpu_get_name(nvmlDevice_t device, char *buf, unsigned int len);
int  gpu_enable_persistence(void);
int  gpu_get_utilization(nvmlDevice_t device);
int  gpu_get_memory(nvmlDevice_t device, unsigned long long *used, unsigned long long *total);
int  gpu_get_power(nvmlDevice_t device);
int  gpu_get_power_limit(nvmlDevice_t device);

#endif /* NVFD_GPU_H */
