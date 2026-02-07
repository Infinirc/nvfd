#include <stdio.h>
#include <string.h>
#include "gpu.h"

int gpu_init(void) {
    nvmlReturn_t r = nvmlInit();
    if (r != NVML_SUCCESS) {
        fprintf(stderr, "Failed to initialize NVML: %s\n", nvmlErrorString(r));
        return -1;
    }

    r = nvmlDeviceGetCount(&device_count);
    if (r != NVML_SUCCESS) {
        fprintf(stderr, "Failed to get device count: %s\n", nvmlErrorString(r));
        nvmlShutdown();
        return -1;
    }

    return 0;
}

void gpu_shutdown(void) {
    nvmlShutdown();
}

int gpu_get_handle(unsigned int index, nvmlDevice_t *device) {
    nvmlReturn_t r = nvmlDeviceGetHandleByIndex(index, device);
    if (r != NVML_SUCCESS) {
        fprintf(stderr, "Failed to get GPU %u handle: %s\n", index, nvmlErrorString(r));
        return -1;
    }
    return 0;
}

int gpu_get_temperature(nvmlDevice_t device) {
    unsigned int temp;
    if (nvmlDeviceGetTemperature(device, NVML_TEMPERATURE_GPU, &temp) == NVML_SUCCESS)
        return (int)temp;
    return -1;
}

int gpu_get_name(nvmlDevice_t device, char *buf, unsigned int len) {
    nvmlReturn_t r = nvmlDeviceGetName(device, buf, len);
    if (r != NVML_SUCCESS) {
        strncpy(buf, "Unknown", len);
        buf[len - 1] = '\0';
        return -1;
    }
    return 0;
}

int gpu_get_utilization(nvmlDevice_t device) {
    nvmlUtilization_t util;
    if (nvmlDeviceGetUtilizationRates(device, &util) == NVML_SUCCESS)
        return (int)util.gpu;
    return -1;
}

int gpu_get_memory(nvmlDevice_t device, unsigned long long *used, unsigned long long *total) {
    nvmlMemory_t mem;
    nvmlReturn_t r = nvmlDeviceGetMemoryInfo(device, &mem);
    if (r != NVML_SUCCESS)
        return -1;
    *used = mem.used;
    *total = mem.total;
    return 0;
}

int gpu_get_power(nvmlDevice_t device) {
    unsigned int power;
    if (nvmlDeviceGetPowerUsage(device, &power) == NVML_SUCCESS)
        return (int)power;
    return -1;
}

int gpu_get_power_limit(nvmlDevice_t device) {
    unsigned int limit;
    if (nvmlDeviceGetEnforcedPowerLimit(device, &limit) == NVML_SUCCESS)
        return (int)limit;
    return -1;
}

int gpu_enable_persistence(void) {
    int failures = 0;
    for (unsigned int i = 0; i < device_count; i++) {
        nvmlDevice_t device;
        if (gpu_get_handle(i, &device) != 0) {
            failures++;
            continue;
        }
        nvmlReturn_t r = nvmlDeviceSetPersistenceMode(device, NVML_FEATURE_ENABLED);
        if (r != NVML_SUCCESS) {
            fprintf(stderr, "Failed to enable persistence on GPU %u: %s\n",
                    i, nvmlErrorString(r));
            failures++;
        }
    }
    return failures;
}
