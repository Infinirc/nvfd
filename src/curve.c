#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <jansson.h>
#include "curve.h"
#include "fan.h"
#include "gpu.h"

static char last_error[512];

const char *curve_last_error(void) {
    return last_error;
}

static int compare_points(const void *a, const void *b) {
    return ((const FanCurvePoint *)a)->temperature -
           ((const FanCurvePoint *)b)->temperature;
}

/* A curve key must be a whole number of degrees, 0-100, with nothing else:
 * no sign, no leading whitespace (strtol would accept both). */
static int parse_temperature(const char *key, int *out) {
    if (!isdigit((unsigned char)key[0]))
        return -1;
    char *end;
    errno = 0;
    long v = strtol(key, &end, 10);
    if (errno != 0 || *end != '\0' || v < 0 || v > 100)
        return -1;
    *out = (int)v;
    return 0;
}

CurveStatus curve_load(FanCurve *curve) {
    struct stat st;
    if (stat(NVFD_CURVE_FILE, &st) != 0) {
        if (errno == ENOENT)
            return CURVE_MISSING;
        snprintf(last_error, sizeof(last_error), "%s: %s",
                 NVFD_CURVE_FILE, strerror(errno));
        return CURVE_INVALID;
    }

    json_error_t error;
    json_t *root = json_load_file(NVFD_CURVE_FILE, JSON_REJECT_DUPLICATES, &error);
    if (!root) {
        snprintf(last_error, sizeof(last_error), "%s: %s (line %d, column %d)",
                 NVFD_CURVE_FILE, error.text, error.line, error.column);
        return CURVE_INVALID;
    }
    if (!json_is_object(root)) {
        snprintf(last_error, sizeof(last_error),
                 "%s: top-level value must be a JSON object", NVFD_CURVE_FILE);
        json_decref(root);
        return CURVE_INVALID;
    }

    curve->point_count = 0;

    const char *key;
    json_t *value;
    json_object_foreach(root, key, value) {
        if (curve->point_count >= MAX_CURVE_POINTS) {
            snprintf(last_error, sizeof(last_error), "%s: more than %d points",
                     NVFD_CURVE_FILE, MAX_CURVE_POINTS);
            json_decref(root);
            return CURVE_INVALID;
        }

        int temp;
        if (parse_temperature(key, &temp) != 0) {
            snprintf(last_error, sizeof(last_error),
                     "%s: key \"%s\" is not a temperature in 0-100", NVFD_CURVE_FILE, key);
            json_decref(root);
            return CURVE_INVALID;
        }
        if (!json_is_integer(value)) {
            snprintf(last_error, sizeof(last_error),
                     "%s: value for %d°C is not an integer", NVFD_CURVE_FILE, temp);
            json_decref(root);
            return CURVE_INVALID;
        }
        json_int_t speed = json_integer_value(value);
        if (speed < 0 || speed > 100) {
            snprintf(last_error, sizeof(last_error),
                     "%s: fan speed %lld for %d°C is outside 0-100",
                     NVFD_CURVE_FILE, (long long)speed, temp);
            json_decref(root);
            return CURVE_INVALID;
        }
        /* Two keys that parse to the same degree ("30" and "030") would make
         * the interpolation divide by zero. */
        for (int i = 0; i < curve->point_count; i++) {
            if (curve->points[i].temperature == temp) {
                snprintf(last_error, sizeof(last_error),
                         "%s: temperature %d°C appears more than once", NVFD_CURVE_FILE, temp);
                json_decref(root);
                return CURVE_INVALID;
            }
        }

        curve->points[curve->point_count].temperature = temp;
        curve->points[curve->point_count].fan_speed = (int)speed;
        curve->point_count++;
    }
    json_decref(root);

    if (curve->point_count == 0) {
        snprintf(last_error, sizeof(last_error), "%s: curve has no points", NVFD_CURVE_FILE);
        return CURVE_INVALID;
    }

    qsort(curve->points, (size_t)curve->point_count, sizeof(FanCurvePoint),
          compare_points);
    return CURVE_OK;
}

FanCurve *curve_read(void) {
    FanCurve *curve = malloc(sizeof(FanCurve));
    if (!curve)
        return NULL;

    CurveStatus status = curve_load(curve);
    if (status == CURVE_OK)
        return curve;
    if (status == CURVE_INVALID)
        fprintf(stderr, "%s\n", last_error);
    free(curve);
    return NULL;
}

int curve_require(FanCurve *curve) {
    CurveStatus status = curve_load(curve);
    if (status == CURVE_MISSING) {
        fprintf(stderr, "%s does not exist; run 'nvfd curve reset' to create it.\n",
                NVFD_CURVE_FILE);
        return -1;
    }
    if (status == CURVE_INVALID) {
        fprintf(stderr, "%s\n", last_error);
        return -1;
    }
    return 0;
}

int curve_write(const FanCurve *curve) {
    json_t *root = json_object();
    if (!root)
        return -1;

    for (int i = 0; i < curve->point_count; i++) {
        char key[8];
        snprintf(key, sizeof(key), "%d", curve->points[i].temperature);
        json_object_set_new(root, key, json_integer(curve->points[i].fan_speed));
    }

    /* Atomic write: write to .tmp then rename */
    char tmp_path[256];
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", NVFD_CURVE_FILE);

    int ret = json_dump_file(root, tmp_path, JSON_INDENT(4) | JSON_SORT_KEYS);
    json_decref(root);
    if (ret != 0) {
        fprintf(stderr, "Failed to write curve file\n");
        remove(tmp_path);
        return -1;
    }

    if (rename(tmp_path, NVFD_CURVE_FILE) != 0) {
        perror("Failed to rename curve file");
        remove(tmp_path);
        return -1;
    }

    return 0;
}

int curve_edit(int temp, int speed) {
    FanCurve curve;
    CurveStatus status = curve_load(&curve);
    if (status == CURVE_INVALID) {
        /* Refuse to overwrite a file we could not parse. */
        fprintf(stderr, "%s\n", last_error);
        return -1;
    }
    if (status == CURVE_MISSING)
        curve.point_count = 0;

    /* Find existing point or insertion position */
    int index = -1;
    for (int i = 0; i < curve.point_count; i++) {
        if (curve.points[i].temperature >= temp) {
            index = i;
            break;
        }
    }
    if (index == -1)
        index = curve.point_count;

    if (index < curve.point_count && curve.points[index].temperature == temp) {
        /* Update existing point */
        curve.points[index].fan_speed = speed;
    } else {
        if (curve.point_count >= MAX_CURVE_POINTS) {
            fprintf(stderr, "Error: Fan curve points have reached the maximum of %d.\n",
                    MAX_CURVE_POINTS);
            return -1;
        }
        /* Insert new point */
        for (int i = curve.point_count; i > index; i--)
            curve.points[i] = curve.points[i - 1];
        curve.points[index].temperature = temp;
        curve.points[index].fan_speed = speed;
        curve.point_count++;
    }

    if (curve_write(&curve) != 0)
        return -1;
    printf("Updated fan curve: %d°C -> %d%%\n", temp, speed);
    return 0;
}

int curve_reset(void) {
    FanCurve def = {
        .points = {
            {30, 30}, {40, 40}, {50, 55},
            {60, 65}, {70, 85}, {80, 100}
        },
        .point_count = 6
    };
    if (curve_write(&def) != 0)
        return -1;
    printf("Fan curve has been reset to default values.\n");
    return 0;
}

int curve_interpolate(int temp, const FanCurve *curve) {
    if (curve->point_count == 0)
        return FAN_SPEED_MIN;

    /* Below first point: use first point's speed */
    if (temp <= curve->points[0].temperature)
        return curve->points[0].fan_speed;

    /* Above last point: use last point's speed */
    if (temp >= curve->points[curve->point_count - 1].temperature)
        return curve->points[curve->point_count - 1].fan_speed;

    /* Linear interpolation between points */
    for (int i = 0; i < curve->point_count - 1; i++) {
        if (temp >= curve->points[i].temperature &&
            temp < curve->points[i + 1].temperature) {
            int t_diff = curve->points[i + 1].temperature -
                         curve->points[i].temperature;
            int s_diff = curve->points[i + 1].fan_speed -
                         curve->points[i].fan_speed;
            int t_off  = temp - curve->points[i].temperature;
            return curve->points[i].fan_speed + (t_off * s_diff) / t_diff;
        }
    }

    return curve->points[curve->point_count - 1].fan_speed;
}

int curve_apply_to_gpu(unsigned int gpu_index) {
    FanCurve curve;
    if (curve_require(&curve) != 0)
        return -1;

    nvmlDevice_t device;
    if (gpu_get_handle(gpu_index, &device) != 0)
        return -1;

    int temp = gpu_get_temperature(device);
    if (temp < 0) {
        fprintf(stderr, "GPU %u: failed to read temperature\n", gpu_index);
        return -1;
    }

    return fan_set_gpu_speed(gpu_index, (unsigned int)curve_interpolate(temp, &curve));
}
