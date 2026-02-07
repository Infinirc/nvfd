#ifndef NVFD_CONFIG_H
#define NVFD_CONFIG_H

#include <jansson.h>
#include "nvfd.h"

int     config_ensure_dir(void);
json_t *config_read(void);
int     config_write_gpu(const char *gpu_key, const char *mode, int speed);
int     config_migrate(void);

#endif /* NVFD_CONFIG_H */
