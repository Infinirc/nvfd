#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>
#include "config.h"

int config_ensure_dir(void) {
    struct stat st;
    if (stat(NVFD_CONFIG_DIR, &st) == 0 && S_ISDIR(st.st_mode))
        return 0;
    if (mkdir(NVFD_CONFIG_DIR, 0755) != 0 && errno != EEXIST) {
        perror("Failed to create config directory");
        return -1;
    }
    return 0;
}

json_t *config_read(void) {
    json_error_t error;
    json_t *root = json_load_file(NVFD_CONFIG_FILE, 0, &error);
    if (!root)
        return json_object(); /* empty config */
    return root;
}

int config_write_gpu(const char *gpu_key, const char *mode, int speed) {
    config_ensure_dir();

    json_error_t error;
    json_t *root = json_load_file(NVFD_CONFIG_FILE, 0, &error);
    if (!root)
        root = json_object();

    json_t *gpu_config = json_object();
    json_object_set_new(gpu_config, "mode", json_string(mode));
    if (strcmp(mode, "manual") == 0)
        json_object_set_new(gpu_config, "speed", json_integer(speed));

    json_object_set_new(root, gpu_key, gpu_config);

    /* Atomic write */
    char tmp_path[256];
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", NVFD_CONFIG_FILE);

    int ret = json_dump_file(root, tmp_path, JSON_INDENT(2));
    json_decref(root);
    if (ret != 0) {
        remove(tmp_path);
        return -1;
    }

    if (rename(tmp_path, NVFD_CONFIG_FILE) != 0) {
        perror("Failed to rename config file");
        remove(tmp_path);
        return -1;
    }

    return 0;
}

int config_migrate(void) {
    struct stat st;

    /* Migrate old curve file */
    if (stat(NVFD_OLD_CURVE_FILE, &st) == 0 && stat(NVFD_CURVE_FILE, &st) != 0) {
        config_ensure_dir();
        json_error_t error;
        json_t *root = json_load_file(NVFD_OLD_CURVE_FILE, 0, &error);
        if (root) {
            json_dump_file(root, NVFD_CURVE_FILE, JSON_INDENT(4) | JSON_SORT_KEYS);
            json_decref(root);
            printf("Migrated fan curve: %s -> %s\n",
                   NVFD_OLD_CURVE_FILE, NVFD_CURVE_FILE);
        }
    }

    /* Migrate old config file */
    if (stat(NVFD_OLD_CONFIG_FILE, &st) == 0 && stat(NVFD_CONFIG_FILE, &st) != 0) {
        config_ensure_dir();

        /* Try parsing as JSON first (the write_config_for_gpu format) */
        json_error_t error;
        json_t *root = json_load_file(NVFD_OLD_CONFIG_FILE, 0, &error);
        if (root) {
            json_dump_file(root, NVFD_CONFIG_FILE, JSON_INDENT(2));
            json_decref(root);
            printf("Migrated config (JSON): %s -> %s\n",
                   NVFD_OLD_CONFIG_FILE, NVFD_CONFIG_FILE);
            return 0;
        }

        /* Fall back to plain-text format */
        FILE *fp = fopen(NVFD_OLD_CONFIG_FILE, "r");
        if (!fp)
            return -1;

        char buf[64];
        if (fgets(buf, sizeof(buf), fp)) {
            buf[strcspn(buf, "\n")] = '\0';

            root = json_object();
            json_t *gpu_config = json_object();

            if (strcmp(buf, "auto") == 0) {
                json_object_set_new(gpu_config, "mode", json_string("auto"));
            } else if (strcmp(buf, "curve") == 0) {
                json_object_set_new(gpu_config, "mode", json_string("curve"));
            } else {
                int speed = atoi(buf);
                if (speed >= 0 && speed <= 100) {
                    json_object_set_new(gpu_config, "mode", json_string("manual"));
                    json_object_set_new(gpu_config, "speed", json_integer(speed));
                } else {
                    json_object_set_new(gpu_config, "mode", json_string("auto"));
                }
            }

            json_object_set_new(root, "gpu0", gpu_config);
            json_dump_file(root, NVFD_CONFIG_FILE, JSON_INDENT(2));
            json_decref(root);
            printf("Migrated config (plain text): %s -> %s\n",
                   NVFD_OLD_CONFIG_FILE, NVFD_CONFIG_FILE);
        }
        fclose(fp);
    }

    return 0;
}
