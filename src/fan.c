#include <stdio.h>
#include "fan.h"
#include "gpu.h"

#define FAN_SPEED_MIN 30

int fan_get_count(nvmlDevice_t device) {
    unsigned int count = 0;
    nvmlReturn_t r = nvmlDeviceGetNumFans(device, &count);
    if (r != NVML_SUCCESS) {
        fprintf(stderr, "Failed to get fan count: %s\n", nvmlErrorString(r));
        return 0;
    }
    return (int)count;
}

int fan_get_speed(nvmlDevice_t device, unsigned int fan) {
    unsigned int speed = 0;
    nvmlReturn_t r = nvmlDeviceGetFanSpeed_v2(device, fan, &speed);
    if (r != NVML_SUCCESS)
        return -1;
    return (int)speed;
}

int fan_set_speed(nvmlDevice_t device, unsigned int fan, unsigned int speed) {
    if (speed < FAN_SPEED_MIN)
        speed = FAN_SPEED_MIN;
    if (speed > 100)
        speed = 100;
    nvmlReturn_t r = nvmlDeviceSetFanSpeed_v2(device, fan, speed);
    if (r != NVML_SUCCESS) {
        fprintf(stderr, "Failed to set fan %u speed: %s\n", fan, nvmlErrorString(r));
        return -1;
    }
    return 0;
}

int fan_set_gpu_speed(unsigned int gpu_index, unsigned int speed) {
    nvmlDevice_t device;
    if (gpu_get_handle(gpu_index, &device) != 0)
        return -1;

    int num_fans = fan_get_count(device);
    if (num_fans <= 0) {
        fprintf(stderr, "Error: No fans detected on GPU %u\n", gpu_index);
        return -1;
    }

    int failures = 0;
    for (int i = 0; i < num_fans; i++) {
        if (fan_set_speed(device, (unsigned int)i, speed) != 0)
            failures++;
    }
    return failures;
}

int fan_set_all_speed(unsigned int speed) {
    int failures = 0;
    for (unsigned int i = 0; i < device_count; i++) {
        if (fan_set_gpu_speed(i, speed) != 0)
            failures++;
    }
    return failures;
}

int fan_reset_to_auto(unsigned int gpu_index) {
    nvmlDevice_t device;
    if (gpu_get_handle(gpu_index, &device) != 0)
        return -1;

    int num_fans = fan_get_count(device);
    int failures = 0;
    for (int i = 0; i < num_fans; i++) {
        nvmlReturn_t r = nvmlDeviceSetDefaultFanSpeed_v2(device, (unsigned int)i);
        if (r != NVML_SUCCESS) {
            fprintf(stderr, "Failed to reset fan %d on GPU %u: %s\n",
                    i, gpu_index, nvmlErrorString(r));
            failures++;
        }
    }

    /* Restore automatic fan policy */
    for (int i = 0; i < num_fans; i++) {
        nvmlDeviceSetFanControlPolicy(device, (unsigned int)i,
                                      NVML_FAN_POLICY_TEMPERATURE_CONTINOUS_SW);
    }

    return failures;
}

void fan_reset_all_to_auto(void) {
    for (unsigned int i = 0; i < device_count; i++)
        fan_reset_to_auto(i);
}
