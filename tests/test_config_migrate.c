#define _GNU_SOURCE
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "config.h"

static void clear_files(void) {
    unlink(NVFD_CONFIG_FILE);
    unlink(NVFD_CURVE_FILE);
    unlink(NVFD_OLD_CONFIG_FILE);
    unlink(NVFD_OLD_CURVE_FILE);
}

static void assert_migration(const char *legacy_value, const char *mode,
                             int expected_speed) {
    clear_files();
    assert(mkdir(NVFD_CONFIG_DIR, 0700) == 0 || access(NVFD_CONFIG_DIR, F_OK) == 0);

    FILE *legacy = fopen(NVFD_OLD_CONFIG_FILE, "w");
    assert(legacy != NULL);
    assert(fputs(legacy_value, legacy) >= 0);
    assert(fclose(legacy) == 0);

    assert(config_migrate() == 0);
    json_t *root = config_read();
    assert(root != NULL);
    assert(json_is_object(root));
    json_t *gpu = json_object_get(root, "gpu0");
    assert(json_is_object(gpu));
    assert(strcmp(json_string_value(json_object_get(gpu, "mode")), mode) == 0);
    if (expected_speed >= 0)
        assert(json_integer_value(json_object_get(gpu, "speed")) == expected_speed);
    json_decref(root);
}

int main(void) {
    char sandbox[] = "/tmp/nvfd-config-migrate-XXXXXX";
    assert(mkdtemp(sandbox) != NULL);
    assert(chdir(sandbox) == 0);
    assert(mkdir("etc", 0700) == 0);

    assert_migration("20\n", "manual", 30);
    assert_migration("0\n", "manual", 30);
    assert_migration("60%\n", "manual", 60);
    assert_migration("abc\n", "auto", -1);
    assert_migration("[]\n", "auto", -1);

    clear_files();
    assert(rmdir(NVFD_CONFIG_DIR) == 0);
    assert(rmdir("etc") == 0);
    assert(chdir("/") == 0);
    assert(rmdir(sandbox) == 0);
    puts("config migration tests passed");
    return 0;
}
