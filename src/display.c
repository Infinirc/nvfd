#include <stdio.h>
#include <string.h>
#include <jansson.h>
#include "display.h"
#include "gpu.h"
#include "fan.h"
#include "curve.h"
#include "config.h"

void display_help(void) {
    printf("NVIDIA Fan Daemon (NVFD) v%s\n\n", NVFD_VERSION);
    printf("+-----------------------------+-----------------------------------------+\n");
    printf("| Command                     | Description                             |\n");
    printf("+-----------------------------+-----------------------------------------+\n");
    printf("| nvfd                        | Interactive TUI dashboard (on TTY)      |\n");
    printf("+-----------------------------+-----------------------------------------+\n");
    printf("| nvfd auto                   | Return fan control to NVIDIA driver     |\n");
    printf("+-----------------------------+-----------------------------------------+\n");
    printf("| nvfd curve                  | Enable custom fan curve for all GPUs    |\n");
    printf("+-----------------------------+-----------------------------------------+\n");
    printf("| nvfd curve <temp> <speed>   | Edit fan curve point                    |\n");
    printf("+-----------------------------+-----------------------------------------+\n");
    printf("| nvfd curve show             | Show current fan curve                  |\n");
    printf("+-----------------------------+-----------------------------------------+\n");
    printf("| nvfd curve edit             | Interactive curve editor (ncurses)      |\n");
    printf("+-----------------------------+-----------------------------------------+\n");
    printf("| nvfd curve reset            | Reset fan curve to default              |\n");
    printf("+-----------------------------+-----------------------------------------+\n");
    printf("| nvfd <speed>                | Set fixed fan speed for all GPUs (30-100)|\n");
    printf("+-----------------------------+-----------------------------------------+\n");
    printf("| nvfd <gpu_index> <speed>    | Set fixed fan speed for specific GPU    |\n");
    printf("+-----------------------------+-----------------------------------------+\n");
    printf("| nvfd list                   | List all GPUs and their indices         |\n");
    printf("+-----------------------------+-----------------------------------------+\n");
    printf("| nvfd status                 | Show current status                     |\n");
    printf("+-----------------------------+-----------------------------------------+\n");
    printf("| nvfd -h                     | Show this help message                  |\n");
    printf("+-----------------------------+-----------------------------------------+\n");
}

void display_status(void) {
    json_t *root = config_read();

    printf("\n==================================================\n");
    printf("NVFD v%s - GPU Status\n", NVFD_VERSION);
    printf("==================================================\n");

    for (unsigned int i = 0; i < device_count; i++) {
        char gpu_key[20];
        snprintf(gpu_key, sizeof(gpu_key), "gpu%d", i);
        json_t *cfg = json_object_get(root, gpu_key);

        nvmlDevice_t device;
        if (gpu_get_handle(i, &device) != 0)
            continue;

        char name[NVML_DEVICE_NAME_BUFFER_SIZE];
        gpu_get_name(device, name, sizeof(name));

        int temp = gpu_get_temperature(device);
        int num_fans = fan_get_count(device);

        printf("GPU %u: %s\n", i, name);

        if (json_is_object(cfg)) {
            const char *mode = json_string_value(json_object_get(cfg, "mode"));
            if (mode && strcmp(mode, "manual") == 0) {
                int speed = (int)json_integer_value(json_object_get(cfg, "speed"));
                printf("  Mode: Fixed speed %d%%\n", speed);
            } else if (mode && strcmp(mode, "curve") == 0) {
                printf("  Mode: Custom curve\n");
            } else {
                printf("  Mode: Auto (driver-controlled)\n");
            }
        } else {
            printf("  Mode: Auto (driver-controlled)\n");
        }

        printf("  Temperature: %d°C\n", temp);

        for (int f = 0; f < num_fans; f++) {
            int spd = fan_get_speed(device, (unsigned int)f);
            if (spd >= 0)
                printf("  Fan %d: %d%%\n", f, spd);
        }
        printf("\n");
    }

    json_decref(root);
}

void display_list_gpus(void) {
    printf("Detected GPUs:\n");
    for (unsigned int i = 0; i < device_count; i++) {
        nvmlDevice_t device;
        if (gpu_get_handle(i, &device) != 0)
            continue;

        char name[NVML_DEVICE_NAME_BUFFER_SIZE];
        gpu_get_name(device, name, sizeof(name));

        int num_fans = fan_get_count(device);
        printf("  GPU %u: %s (%d fan%s)\n", i, name, num_fans,
               num_fans != 1 ? "s" : "");
    }
}

void display_fan_curve(void) {
    FanCurve *curve = curve_read();
    if (curve) {
        printf("Current fan curve:\n");
        printf("+--------------+-----------------+\n");
        printf("| Temperature  | Fan Speed       |\n");
        printf("+--------------+-----------------+\n");
        for (int i = 0; i < curve->point_count; i++) {
            printf("| %6d °C    | %6d %%        |\n",
                   curve->points[i].temperature,
                   curve->points[i].fan_speed);
        }
        printf("+--------------+-----------------+\n");
        free(curve);
    } else {
        printf("Fan curve is not set. Use 'nvfd curve reset' to create default.\n");
    }
}
