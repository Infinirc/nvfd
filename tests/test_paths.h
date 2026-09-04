#ifndef NVFD_TEST_PATHS_H
#define NVFD_TEST_PATHS_H

/* Skip nvfd.h so config.c can be tested without NVML headers. */
#define NVFD_H
#define NVFD_CONFIG_DIR "etc/nvfd"
#define NVFD_CONFIG_FILE "etc/nvfd/config.json"
#define NVFD_CURVE_FILE "etc/nvfd/curve.json"
#define NVFD_OLD_CONFIG_FILE "etc/legacy.conf"
#define NVFD_OLD_CURVE_FILE "etc/legacy-curve.json"

#endif /* NVFD_TEST_PATHS_H */
