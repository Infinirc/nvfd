#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
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
#include "notify.h"

#define POLL_INTERVAL_SEC 5

unsigned int device_count = 0;
volatile sig_atomic_t keep_running = 1;
volatile sig_atomic_t reload_config = 0;

static void signal_handler(int signum) {
    if (signum == SIGTERM || signum == SIGINT)
        keep_running = 0;
    else if (signum == SIGHUP)
        reload_config = 1;
}

/*
 * Fatal error in daemon mode. Log it, hand the fans back to the driver so a
 * dead daemon never leaves them pinned at the last speed it set, and exit
 * non-zero so systemd's Restart=on-failure takes over.
 */
__attribute__((noreturn))
static void daemon_die(const char *msg) {
    syslog(LOG_ERR, "%s", msg);
    fprintf(stderr, "nvfd: %s\n", msg);

    syslog(LOG_INFO, "Resetting fans to auto before exit");
    int unreset = fan_reset_all_to_auto();
    if (unreset != 0)
        syslog(LOG_ERR, "%d GPU(s) could not be reset to auto", unreset);

    if (notify_send("STOPPING=1") < 0)
        syslog(LOG_WARNING, "sd_notify(STOPPING=1) failed: %s", strerror(errno));
    closelog();
    exit(EXIT_FAILURE);
}

__attribute__((noreturn, format(printf, 1, 2)))
static void daemon_dief(const char *fmt, ...) {
    char msg[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    daemon_die(msg);
}

static void notify_or_die(const char *state) {
    if (notify_send(state) < 0)
        daemon_dief("sd_notify(%s) failed: %s", state, strerror(errno));
}

/* Fan speed for a GPU in manual mode, taken from its config entry. */
static int manual_speed(unsigned int gpu, json_t *cfg) {
    json_t *speed = json_object_get(cfg, "speed");
    if (!json_is_integer(speed))
        daemon_dief("GPU %u: manual mode without an integer \"speed\" in %s",
                    gpu, NVFD_CONFIG_FILE);
    json_int_t value = json_integer_value(speed);
    if (value < FAN_SPEED_MIN || value > FAN_SPEED_MAX)
        daemon_dief("GPU %u: manual speed %lld is outside %d-%d",
                    gpu, (long long)value, FAN_SPEED_MIN, FAN_SPEED_MAX);
    return (int)value;
}

/* Fan speed for a GPU in curve mode, from its current temperature. */
static int curve_speed(unsigned int gpu, const FanCurve *curve) {
    nvmlDevice_t device;
    if (gpu_get_handle(gpu, &device) != 0)
        daemon_dief("GPU %u: failed to get device handle", gpu);
    int temp = gpu_get_temperature(device);
    if (temp < 0)
        daemon_dief("GPU %u: failed to read temperature", gpu);
    return curve_interpolate(temp, curve);
}

/* systemd passes the configured WatchdogSec in microseconds. Pinging once per
 * poll only works if the poll interval leaves margin under it. */
static void check_watchdog_interval(void) {
    const char *usec = getenv("WATCHDOG_USEC");
    if (!usec || !*usec)
        return;
    char *end;
    errno = 0;
    unsigned long long watchdog_usec = strtoull(usec, &end, 10);
    if (errno != 0 || end == usec || *end != '\0')
        daemon_dief("WATCHDOG_USEC=\"%s\" is not a number", usec);
    unsigned long long required_usec = 2ULL * POLL_INTERVAL_SEC * 1000000ULL;
    if (watchdog_usec < required_usec)
        daemon_dief("WatchdogSec is %llu us but the %ds poll needs at least %llu us",
                    watchdog_usec, POLL_INTERVAL_SEC, required_usec);
}

static void daemon_loop(void) {
    /* Nothing is known about the fan state a previous instance left behind
     * (it may have died without resetting), so the first poll hands every
     * GPU that is not ours to manage back to the driver. */
    int prev_managed[MAX_GPU_COUNT];
    for (unsigned int i = 0; i < MAX_GPU_COUNT; i++)
        prev_managed[i] = 1;
    int first_poll = 1;

    printf("Entering daemon mode (polling every %ds)...\n", POLL_INTERVAL_SEC);
    openlog("nvfd", LOG_PID, LOG_DAEMON);
    check_watchdog_interval();
    notify_or_die("READY=1");

    while (keep_running) {
        if (reload_config) {
            syslog(LOG_INFO, "SIGHUP received: config and curve are re-read every "
                             "poll, nothing to reload");
            reload_config = 0;
        }

        json_t *root = config_read();
        if (!root)
            daemon_die(config_last_error());

        /* Loaded on first use each poll, so edits take effect within one interval. */
        FanCurve curve;
        int curve_loaded = 0;

        for (unsigned int i = 0; i < device_count; i++) {
            char gpu_key[20];
            snprintf(gpu_key, sizeof(gpu_key), "gpu%u", i);
            json_t *cfg = json_object_get(root, gpu_key);

            const char *mode = NULL;
            if (json_is_object(cfg))
                mode = json_string_value(json_object_get(cfg, "mode"));

            /* No entry, or auto: the driver owns the fans. */
            if (!mode || strcmp(mode, "auto") == 0) {
                if (prev_managed[i]) {
                    syslog(LOG_INFO, "GPU %u: restoring driver fan control", i);
                    if (fan_reset_to_auto(i) != 0) {
                        if (!first_poll)
                            daemon_dief("GPU %u: failed to restore driver fan control", i);
                        /* Startup sweep of a GPU this instance never touched:
                         * a card without controllable fans lands here, and
                         * that must not stop the daemon managing the others. */
                        syslog(LOG_WARNING, "GPU %u: could not hand fans to the driver "
                                            "at startup; continuing", i);
                    }
                    prev_managed[i] = 0;
                }
                continue;
            }

            int fan_speed;
            if (strcmp(mode, "manual") == 0) {
                fan_speed = manual_speed(i, cfg);
            } else if (strcmp(mode, "curve") == 0) {
                if (!curve_loaded) {
                    CurveStatus status = curve_load(&curve);
                    if (status == CURVE_MISSING)
                        daemon_dief("GPU %u is in curve mode but %s does not exist; "
                                    "run 'nvfd curve reset'", i, NVFD_CURVE_FILE);
                    if (status == CURVE_INVALID)
                        daemon_die(curve_last_error());
                    curve_loaded = 1;
                }
                fan_speed = curve_speed(i, &curve);
            } else {
                daemon_dief("GPU %u: unknown mode \"%s\" in %s", i, mode, NVFD_CONFIG_FILE);
            }

            if (fan_set_gpu_speed(i, (unsigned int)fan_speed) != 0)
                daemon_dief("GPU %u: failed to set fan speed to %d%%", i, fan_speed);
            prev_managed[i] = 1;
        }

        json_decref(root);
        first_poll = 0;
        notify_or_die("WATCHDOG=1");
        sleep(POLL_INTERVAL_SEC);
    }

    /* Clean shutdown: reset first, then tell systemd we are going. */
    syslog(LOG_INFO, "Shutting down, resetting fans to auto...");
    int unreset = fan_reset_all_to_auto();
    if (notify_send("STOPPING=1") < 0)
        syslog(LOG_WARNING, "sd_notify(STOPPING=1) failed: %s", strerror(errno));
    if (unreset != 0) {
        syslog(LOG_ERR, "%d GPU(s) could not be reset to auto", unreset);
        closelog();
        exit(EXIT_FAILURE);
    }
    closelog();
}

/* Enabling curve mode is only meaningful if the curve can actually be loaded. */
static int require_curve(void) {
    FanCurve curve;
    return curve_require(&curve);
}

static int set_gpu_mode(unsigned int gpu, const char *mode, int speed) {
    char gpu_key[20];
    snprintf(gpu_key, sizeof(gpu_key), "gpu%u", gpu);
    return config_write_gpu(gpu_key, mode, speed);
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
    if (config_migrate() != 0) {
        gpu_shutdown();
        return 1;
    }

    signal(SIGTERM, signal_handler);
    signal(SIGINT, signal_handler);
    signal(SIGHUP, signal_handler);

    int rc = 0;

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
        rc = display_status();
    } else if (strcmp(argv[1], "auto") == 0) {
        /* True auto: hand control back to driver */
        for (unsigned int i = 0; i < device_count; i++) {
            if (set_gpu_mode(i, "auto", 0) != 0 || fan_reset_to_auto(i) != 0)
                rc = -1;
        }
        if (rc == 0)
            printf("All GPU fans set to auto (driver-controlled).\n");
    } else if (strcmp(argv[1], "curve") == 0) {
        if (argc == 2) {
            /* Enable curve mode for all GPUs */
            rc = require_curve();
            for (unsigned int i = 0; rc == 0 && i < device_count; i++)
                rc = set_gpu_mode(i, "curve", 0);
            if (rc == 0)
                printf("All GPUs set to curve mode.\n");
        } else if (argc == 4) {
            int temp = atoi(argv[2]);
            int speed = atoi(argv[3]);
            if (temp >= 0 && temp <= 100 && speed >= 0 && speed <= 100) {
                rc = config_ensure_dir();
                if (rc == 0)
                    rc = curve_edit(temp, speed);
            } else {
                printf("Invalid input. Temperature and speed must be 0-100.\n");
                rc = -1;
            }
        } else if (argc == 3) {
            if (strcmp(argv[2], "show") == 0) {
                rc = display_fan_curve();
            } else if (strcmp(argv[2], "edit") == 0) {
                rc = config_ensure_dir();
                if (rc == 0) {
                    rc = editor_run();
                    if (rc != 0)
                        fprintf(stderr, "%s\n", curve_last_error());
                }
            } else if (strcmp(argv[2], "reset") == 0) {
                rc = config_ensure_dir();
                if (rc == 0)
                    rc = curve_reset();
            } else {
                printf("Invalid curve command. Use 'show', 'edit', or 'reset'.\n");
                display_help();
                rc = -1;
            }
        } else {
            printf("Invalid curve command.\n");
            display_help();
            rc = -1;
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

            /* Per-GPU mode keywords */
            if (gpu_index >= 0 && gpu_index < (int)device_count) {
                if (strcmp(argv[2], "auto") == 0) {
                    if (set_gpu_mode((unsigned int)gpu_index, "auto", 0) != 0 ||
                        fan_reset_to_auto((unsigned int)gpu_index) != 0)
                        rc = -1;
                    else
                        printf("GPU %d set to auto mode (driver-controlled).\n", gpu_index);
                } else if (strcmp(argv[2], "curve") == 0) {
                    rc = require_curve();
                    if (rc == 0)
                        rc = set_gpu_mode((unsigned int)gpu_index, "curve", 0);
                    if (rc == 0)
                        rc = curve_apply_to_gpu((unsigned int)gpu_index);
                    if (rc == 0)
                        printf("GPU %d set to curve mode.\n", gpu_index);
                } else {
                    /* Treat as speed */
                    speed = atoi(argv[2]);
                }
            } else {
                printf("Invalid GPU index. Use 'nvfd list' to see available GPUs.\n");
                display_help();
                gpu_shutdown();
                return 1;
            }
        } else if (argc == 4) {
            /* Per-GPU manual mode: nvfd <gpu_index> manual <speed> */
            int gpu_idx = atoi(argv[1]);
            if (strcmp(argv[2], "manual") != 0) {
                printf("Invalid command: %s\n", argv[2]);
                display_help();
                gpu_shutdown();
                return 1;
            } else if (gpu_idx < 0 || gpu_idx >= (int)device_count) {
                printf("Invalid GPU index. Use 'nvfd list' to see available GPUs.\n");
                display_help();
                gpu_shutdown();
                return 1;
            } else {
                speed = atoi(argv[3]);
                gpu_index = gpu_idx;
            }
        }

        if (speed >= FAN_SPEED_MIN && speed <= FAN_SPEED_MAX) {
            if (gpu_index == -1) {
                /* Set all GPUs */
                for (unsigned int i = 0; i < device_count; i++) {
                    if (set_gpu_mode(i, "manual", speed) != 0 ||
                        fan_set_gpu_speed(i, (unsigned int)speed) != 0)
                        rc = -1;
                }
                if (rc == 0)
                    printf("All GPUs set to fixed speed %d%%.\n", speed);
            } else {
                if (set_gpu_mode((unsigned int)gpu_index, "manual", speed) != 0 ||
                    fan_set_gpu_speed((unsigned int)gpu_index, (unsigned int)speed) != 0)
                    rc = -1;
                else
                    printf("GPU %d set to fixed speed %d%%.\n", gpu_index, speed);
            }
        } else if (speed != -1) {
            printf("Invalid speed. Use a value between %d and %d.\n",
                   FAN_SPEED_MIN, FAN_SPEED_MAX);
            display_help();
            rc = -1;
        }
    }

    gpu_shutdown();
    return rc == 0 ? 0 : 1;
}
