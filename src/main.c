#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <syslog.h>
#include <signal.h>
#include <jansson.h>

#include "nvfd.h"
#include "gpu.h"
#include "fan.h"
#include "curve.h"
#include "config.h"
#include "display.h"
#include "editor.h"
#include "dashboard.h"

unsigned int device_count = 0;
volatile sig_atomic_t keep_running = 1;
volatile sig_atomic_t reload_config = 0;

static void signal_handler(int signum) {
    if (signum == SIGTERM || signum == SIGINT)
        keep_running = 0;
    else if (signum == SIGHUP)
        reload_config = 1;
}

static void daemon_loop(void) {
    FanCurve *curve = NULL;
    int prev_managed[MAX_GPU_COUNT] = {0};

    printf("Entering daemon mode (polling every 5s)...\n");
    openlog("nvfd", LOG_PID, LOG_DAEMON);

    while (keep_running) {
        if (reload_config) {
            syslog(LOG_INFO, "Reloading configuration (SIGHUP)");
            if (curve) { free(curve); curve = NULL; }
            reload_config = 0;
        }

        json_t *root = config_read();

        for (unsigned int i = 0; i < device_count; i++) {
            char gpu_key[20];
            snprintf(gpu_key, sizeof(gpu_key), "gpu%d", i);
            json_t *cfg = json_object_get(root, gpu_key);

            nvmlDevice_t device;
            if (gpu_get_handle(i, &device) != 0)
                continue;

            const char *mode = NULL;
            if (json_is_object(cfg))
                mode = json_string_value(json_object_get(cfg, "mode"));

            /* No config or auto mode: let driver control fans */
            if (!mode || strcmp(mode, "auto") == 0) {
                if (prev_managed[i]) {
                    syslog(LOG_INFO, "GPU %u: restoring driver fan control", i);
                    fan_reset_to_auto(i);
                    prev_managed[i] = 0;
                }
                continue;
            }

            int temp = gpu_get_temperature(device);
            if (temp < 0)
                continue;

            int fan_speed;
            if (mode && strcmp(mode, "manual") == 0) {
                fan_speed = (int)json_integer_value(json_object_get(cfg, "speed"));
            } else if (mode && strcmp(mode, "curve") == 0) {
                if (!curve)
                    curve = curve_read();
                if (curve)
                    fan_speed = curve_interpolate(temp, curve);
                else
                    fan_speed = curve_default_interpolate(temp);
            } else {
                /* Unknown mode - fall back to default curve */
                fan_speed = curve_default_interpolate(temp);
            }

            fan_set_gpu_speed(i, (unsigned int)fan_speed);
            prev_managed[i] = 1;
        }

        json_decref(root);
        sleep(5);
    }

    if (curve)
        free(curve);

    /* Reset all fans to auto on clean shutdown */
    syslog(LOG_INFO, "Shutting down, resetting fans to auto...");
    fan_reset_all_to_auto();
    closelog();
}

int main(int argc, char *argv[]) {
    /* Auto-elevate to root if needed */
    if (geteuid() != 0) {
        char **new_argv = malloc(sizeof(char *) * (argc + 2));
        if (!new_argv) {
            fprintf(stderr, "Memory allocation failed\n");
            return 1;
        }
        new_argv[0] = "sudo";
        for (int i = 0; i < argc; i++)
            new_argv[i + 1] = argv[i];
        new_argv[argc + 1] = NULL;
        execvp("sudo", new_argv);
        perror("Failed to execute sudo");
        free(new_argv);
        return 1;
    }

    if (gpu_init() != 0)
        return 1;

    /* Determine if we should launch TUI (argc==1 on a TTY) */
    int tui_mode = (argc == 1 && isatty(STDIN_FILENO));

    if (!tui_mode) {
        printf("==================================================\n");
        printf("NVFD v%s - GPU Detection\n", NVFD_VERSION);
        printf("==================================================\n");
        printf("Detected %u GPU%s\n", device_count, device_count != 1 ? "s" : "");
        printf("==================================================\n");
    }

    /* Migrate old config files if present */
    config_migrate();

    signal(SIGTERM, signal_handler);
    signal(SIGINT, signal_handler);
    signal(SIGHUP, signal_handler);

    if (argc == 1) {
        if (tui_mode) {
            /* Interactive TUI dashboard */
            dashboard_run();
        } else {
            /* Daemon mode (non-TTY, e.g. systemd) */
            gpu_enable_persistence();
            daemon_loop();
        }
    } else if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
        display_help();
    } else if (strcmp(argv[1], "status") == 0) {
        display_status();
    } else if (strcmp(argv[1], "auto") == 0) {
        /* True auto: hand control back to driver */
        for (unsigned int i = 0; i < device_count; i++) {
            char gpu_key[20];
            snprintf(gpu_key, sizeof(gpu_key), "gpu%d", i);
            config_write_gpu(gpu_key, "auto", 0);
            fan_reset_to_auto(i);
        }
        printf("All GPU fans set to auto (driver-controlled).\n");
    } else if (strcmp(argv[1], "curve") == 0) {
        if (argc == 2) {
            /* Enable curve mode for all GPUs */
            for (unsigned int i = 0; i < device_count; i++) {
                char gpu_key[20];
                snprintf(gpu_key, sizeof(gpu_key), "gpu%d", i);
                config_write_gpu(gpu_key, "curve", 0);
            }
            printf("All GPUs set to curve mode.\n");
        } else if (argc == 4) {
            int temp = atoi(argv[2]);
            int speed = atoi(argv[3]);
            if (temp >= 0 && temp <= 100 && speed >= 0 && speed <= 100) {
                config_ensure_dir();
                curve_edit(temp, speed);
            } else {
                printf("Invalid input. Temperature and speed must be 0-100.\n");
            }
        } else if (argc == 3) {
            if (strcmp(argv[2], "show") == 0) {
                display_fan_curve();
            } else if (strcmp(argv[2], "edit") == 0) {
                config_ensure_dir();
                editor_run();
            } else if (strcmp(argv[2], "reset") == 0) {
                config_ensure_dir();
                curve_reset();
            } else {
                printf("Invalid curve command. Use 'show', 'edit', or 'reset'.\n");
                display_help();
            }
        } else {
            printf("Invalid curve command.\n");
            display_help();
        }
    } else if (strcmp(argv[1], "list") == 0) {
        display_list_gpus();
    } else {
        /* Numeric arguments: set fixed speed */
        int gpu_index = -1;
        int speed = -1;

        if (argc == 2) {
            speed = atoi(argv[1]);
            if (speed == 0 && strcmp(argv[1], "0") != 0) {
                printf("Invalid command: %s\n", argv[1]);
                display_help();
                gpu_shutdown();
                return 1;
            }
        } else if (argc == 3) {
            gpu_index = atoi(argv[1]);
            speed = atoi(argv[2]);
        }

        if (speed >= 30 && speed <= 100) {
            if (gpu_index == -1) {
                /* Set all GPUs */
                for (unsigned int i = 0; i < device_count; i++) {
                    char gpu_key[20];
                    snprintf(gpu_key, sizeof(gpu_key), "gpu%d", i);
                    config_write_gpu(gpu_key, "manual", speed);
                    fan_set_gpu_speed(i, (unsigned int)speed);
                }
                printf("All GPUs set to fixed speed %d%%.\n", speed);
            } else if (gpu_index >= 0 && gpu_index < (int)device_count) {
                char gpu_key[20];
                snprintf(gpu_key, sizeof(gpu_key), "gpu%d", gpu_index);
                config_write_gpu(gpu_key, "manual", speed);
                fan_set_gpu_speed((unsigned int)gpu_index, (unsigned int)speed);
                printf("GPU %d set to fixed speed %d%%.\n", gpu_index, speed);
            } else {
                printf("Invalid GPU index. Use 'nvfd list' to see available GPUs.\n");
            }
        } else {
            printf("Invalid speed. Use a value between 30 and 100.\n");
            display_help();
        }
    }

    gpu_shutdown();
    return 0;
}
