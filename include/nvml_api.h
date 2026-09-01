/*
 * Declarations for the NVML entry points nvfd uses.
 *
 * nvfd deliberately does not include NVIDIA's nvml.h. That header ships only
 * with the CUDA toolkit, and installing the toolkit through a distribution
 * package can replace or pin the NVIDIA driver on the host. The NVML ABI is
 * stable and versioned by symbol name (the _v2 suffixes below), and
 * libnvidia-ml.so.1 ships with every driver, so declaring the handful of
 * functions we call is sufficient. Signatures and enum values match nvml.h
 * from CUDA 12.x; they are part of the ABI and do not change.
 *
 * Minimum driver: R520, the first branch whose libnvidia-ml.so.1 exports
 * every symbol below. nvmlDeviceSetFanSpeed_v2 / SetDefaultFanSpeed_v2
 * appeared in R515; nvmlDeviceSetFanControlPolicy in 520.61.05 (backported
 * to 515.105.01). Per `nm -D` on the NVIDIA rhel8 repo builds of 515.43.04,
 * 515.105.01 and 520.61.05.
 */
#ifndef NVFD_NVML_API_H
#define NVFD_NVML_API_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct nvmlDevice_st *nvmlDevice_t;

/* Only NVML_SUCCESS is compared against; every other code is passed straight
 * to nvmlErrorString(). NVML_ERROR_UNKNOWN is listed so the enum has the full
 * value range nvml.h gives it. */
typedef enum nvmlReturn_enum {
    NVML_SUCCESS       = 0,
    NVML_ERROR_UNKNOWN = 999
} nvmlReturn_t;

typedef enum nvmlEnableState_enum {
    NVML_FEATURE_DISABLED = 0,
    NVML_FEATURE_ENABLED  = 1
} nvmlEnableState_t;

typedef enum nvmlTemperatureSensors_enum {
    NVML_TEMPERATURE_GPU = 0
} nvmlTemperatureSensors_t;

typedef enum nvmlFanControlPolicy_enum {
    NVML_FAN_POLICY_TEMPERATURE_CONTINOUS_SW = 0,   /* sic — NVIDIA's spelling */
    NVML_FAN_POLICY_MANUAL                   = 1
} nvmlFanControlPolicy_t;

typedef struct nvmlUtilization_st {
    unsigned int gpu;
    unsigned int memory;
} nvmlUtilization_t;

/* v1 layout, as consumed by nvmlDeviceGetMemoryInfo (not the _v2 variant). */
typedef struct nvmlMemory_st {
    unsigned long long total;
    unsigned long long free;
    unsigned long long used;
} nvmlMemory_t;

#define NVML_DEVICE_NAME_BUFFER_SIZE 64

nvmlReturn_t nvmlInit_v2(void);
nvmlReturn_t nvmlShutdown(void);
const char  *nvmlErrorString(nvmlReturn_t result);

nvmlReturn_t nvmlDeviceGetCount_v2(unsigned int *deviceCount);
nvmlReturn_t nvmlDeviceGetHandleByIndex_v2(unsigned int index, nvmlDevice_t *device);
nvmlReturn_t nvmlDeviceGetName(nvmlDevice_t device, char *name, unsigned int length);
nvmlReturn_t nvmlDeviceGetTemperature(nvmlDevice_t device,
                                      nvmlTemperatureSensors_t sensorType,
                                      unsigned int *temp);
nvmlReturn_t nvmlDeviceGetUtilizationRates(nvmlDevice_t device,
                                           nvmlUtilization_t *utilization);
nvmlReturn_t nvmlDeviceGetMemoryInfo(nvmlDevice_t device, nvmlMemory_t *memory);
nvmlReturn_t nvmlDeviceGetPowerUsage(nvmlDevice_t device, unsigned int *power);
nvmlReturn_t nvmlDeviceGetEnforcedPowerLimit(nvmlDevice_t device, unsigned int *limit);
nvmlReturn_t nvmlDeviceSetPersistenceMode(nvmlDevice_t device, nvmlEnableState_t mode);

nvmlReturn_t nvmlDeviceGetNumFans(nvmlDevice_t device, unsigned int *numFans);
nvmlReturn_t nvmlDeviceGetFanSpeed_v2(nvmlDevice_t device, unsigned int fan,
                                      unsigned int *speed);
nvmlReturn_t nvmlDeviceSetFanSpeed_v2(nvmlDevice_t device, unsigned int fan,
                                      unsigned int speed);
nvmlReturn_t nvmlDeviceSetDefaultFanSpeed_v2(nvmlDevice_t device, unsigned int fan);
nvmlReturn_t nvmlDeviceSetFanControlPolicy(nvmlDevice_t device, unsigned int fan,
                                           nvmlFanControlPolicy_t policy);

/* nvml.h maps the unversioned names onto the current ABI the same way. */
#define nvmlInit                   nvmlInit_v2
#define nvmlDeviceGetCount         nvmlDeviceGetCount_v2
#define nvmlDeviceGetHandleByIndex nvmlDeviceGetHandleByIndex_v2

#ifdef __cplusplus
}
#endif

#endif /* NVFD_NVML_API_H */
