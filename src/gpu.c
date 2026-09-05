#include <stdio.h>
#include <stddef.h>
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

    /* Per-GPU state lives in fixed-size arrays sized by MAX_GPU_COUNT. */
    if (device_count > MAX_GPU_COUNT) {
        fprintf(stderr, "Found %u GPUs but NVFD supports at most %d "
                        "(MAX_GPU_COUNT in include/nvfd.h)\n",
                device_count, MAX_GPU_COUNT);
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

/* Trip a few degrees before the hardware starts throttling itself, so the fans
 * are already at full speed by the time performance would be affected. */
#define FAILSAFE_MARGIN_C 3

int gpu_get_failsafe_temperature(nvmlDevice_t device) {
    /* GPU_MAX is where the card drops below base clock; SLOWDOWN is the harder
     * hardware limit above it. Prefer the former, fall back to the latter. */
    static const nvmlTemperatureThresholds_t order[] = {
        NVML_TEMPERATURE_THRESHOLD_GPU_MAX,
        NVML_TEMPERATURE_THRESHOLD_SLOWDOWN,
        NVML_TEMPERATURE_THRESHOLD_SHUTDOWN
    };
    for (size_t i = 0; i < sizeof(order) / sizeof(order[0]); i++) {
        unsigned int limit;
        if (nvmlDeviceGetTemperatureThreshold(device, order[i], &limit) != NVML_SUCCESS)
            continue;
        /* Some drivers report these relative to a limit rather than in degrees;
         * anything outside a plausible die temperature is not usable. */
        if (limit < 40 || limit > 120)
            continue;
        return (int)limit - FAILSAFE_MARGIN_C;
    }
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
