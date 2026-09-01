/*
 * Stub libnvidia-ml.so.1 for CI. Every entry point nvfd declares in
 * include/nvml_api.h is defined here with that exact signature, so this file
 * does two things: it lets `make` link on a runner with no driver, and it
 * fails to compile if nvml_api.h drifts from the definitions below.
 *
 * Build: cc -shared -fPIC -Wl,-soname,libnvidia-ml.so.1 -Iinclude \
 *           -o <dir>/libnvidia-ml.so.1 tests/nvml_stub.c
 * Then:  make LDFLAGS=-L<dir>
 */
#include <stddef.h>
#include "nvml_api.h"

nvmlReturn_t nvmlInit_v2(void) { return NVML_SUCCESS; }
nvmlReturn_t nvmlShutdown(void) { return NVML_SUCCESS; }
const char *nvmlErrorString(nvmlReturn_t result) { (void)result; return "stub"; }

nvmlReturn_t nvmlDeviceGetCount_v2(unsigned int *deviceCount) { *deviceCount = 0; return NVML_SUCCESS; }
nvmlReturn_t nvmlDeviceGetHandleByIndex_v2(unsigned int index, nvmlDevice_t *device) { (void)index; *device = NULL; return NVML_SUCCESS; }
nvmlReturn_t nvmlDeviceGetName(nvmlDevice_t device, char *name, unsigned int length) { (void)device; if (length) name[0] = '\0'; return NVML_SUCCESS; }
nvmlReturn_t nvmlDeviceGetTemperature(nvmlDevice_t device, nvmlTemperatureSensors_t sensorType, unsigned int *temp) { (void)device; (void)sensorType; *temp = 0; return NVML_SUCCESS; }
nvmlReturn_t nvmlDeviceGetUtilizationRates(nvmlDevice_t device, nvmlUtilization_t *utilization) { (void)device; utilization->gpu = utilization->memory = 0; return NVML_SUCCESS; }
nvmlReturn_t nvmlDeviceGetMemoryInfo(nvmlDevice_t device, nvmlMemory_t *memory) { (void)device; memory->total = memory->free = memory->used = 0; return NVML_SUCCESS; }
nvmlReturn_t nvmlDeviceGetPowerUsage(nvmlDevice_t device, unsigned int *power) { (void)device; *power = 0; return NVML_SUCCESS; }
nvmlReturn_t nvmlDeviceGetEnforcedPowerLimit(nvmlDevice_t device, unsigned int *limit) { (void)device; *limit = 0; return NVML_SUCCESS; }
nvmlReturn_t nvmlDeviceSetPersistenceMode(nvmlDevice_t device, nvmlEnableState_t mode) { (void)device; (void)mode; return NVML_SUCCESS; }

nvmlReturn_t nvmlDeviceGetNumFans(nvmlDevice_t device, unsigned int *numFans) { (void)device; *numFans = 0; return NVML_SUCCESS; }
nvmlReturn_t nvmlDeviceGetFanSpeed_v2(nvmlDevice_t device, unsigned int fan, unsigned int *speed) { (void)device; (void)fan; *speed = 0; return NVML_SUCCESS; }
nvmlReturn_t nvmlDeviceSetFanSpeed_v2(nvmlDevice_t device, unsigned int fan, unsigned int speed) { (void)device; (void)fan; (void)speed; return NVML_SUCCESS; }
nvmlReturn_t nvmlDeviceSetDefaultFanSpeed_v2(nvmlDevice_t device, unsigned int fan) { (void)device; (void)fan; return NVML_SUCCESS; }
nvmlReturn_t nvmlDeviceSetFanControlPolicy(nvmlDevice_t device, unsigned int fan, nvmlFanControlPolicy_t policy) { (void)device; (void)fan; (void)policy; return NVML_SUCCESS; }
