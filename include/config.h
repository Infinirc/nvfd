#ifndef NVFD_CONFIG_H
#define NVFD_CONFIG_H

#include <jansson.h>
#include "nvfd.h"

int     config_ensure_dir(void);

/* Returns the parsed config object, or an empty object when no config file
 * exists yet. Returns NULL when the file exists but cannot be used; the
 * reason is available from config_last_error(). */
json_t *config_read(void);
const char *config_last_error(void);

int     config_write_gpu(const char *gpu_key, const char *mode, int speed);
int     config_migrate(void);

#endif /* NVFD_CONFIG_H */
