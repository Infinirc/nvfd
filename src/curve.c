#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <jansson.h>
#include "curve.h"

static int compare_points(const void *a, const void *b) {
    return ((const FanCurvePoint *)a)->temperature -
           ((const FanCurvePoint *)b)->temperature;
}

FanCurve *curve_read(void) {
    json_error_t error;
    json_t *root = json_load_file(NVFD_CURVE_FILE, 0, &error);
    if (!root)
        return NULL;

    FanCurve *curve = malloc(sizeof(FanCurve));
    if (!curve) {
        json_decref(root);
        return NULL;
    }
    curve->point_count = 0;

    const char *key;
    json_t *value;
    json_object_foreach(root, key, value) {
        if (curve->point_count >= MAX_CURVE_POINTS)
            break;
        curve->points[curve->point_count].temperature = atoi(key);
        curve->points[curve->point_count].fan_speed = (int)json_integer_value(value);
        curve->point_count++;
    }

    json_decref(root);

    /* Ensure points are sorted by temperature */
    qsort(curve->points, (size_t)curve->point_count, sizeof(FanCurvePoint),
          compare_points);

    return curve;
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

void curve_edit(int temp, int speed) {
    FanCurve *curve = curve_read();
    if (!curve) {
        curve = malloc(sizeof(FanCurve));
        if (!curve) return;
        curve->point_count = 0;
    }

    /* Find existing point or insertion position */
    int index = -1;
    for (int i = 0; i < curve->point_count; i++) {
        if (curve->points[i].temperature == temp) {
            index = i;
            break;
        } else if (curve->points[i].temperature > temp) {
            index = i;
            break;
        }
    }
    if (index == -1)
        index = curve->point_count;

    if (index < curve->point_count && curve->points[index].temperature == temp) {
        /* Update existing point */
        curve->points[index].fan_speed = speed;
    } else {
        if (curve->point_count >= MAX_CURVE_POINTS) {
            printf("Error: Fan curve points have reached the maximum of %d.\n",
                   MAX_CURVE_POINTS);
            free(curve);
            return;
        }
        /* Insert new point */
        for (int i = curve->point_count; i > index; i--)
            curve->points[i] = curve->points[i - 1];
        curve->points[index].temperature = temp;
        curve->points[index].fan_speed = speed;
        curve->point_count++;
    }

    curve_write(curve);
    printf("Updated fan curve: %d°C -> %d%%\n", temp, speed);
    free(curve);
}

void curve_reset(void) {
    FanCurve def = {
        .points = {
            {30, 30}, {40, 40}, {50, 55},
            {60, 65}, {70, 85}, {80, 100}
        },
        .point_count = 6
    };
    curve_write(&def);
    printf("Fan curve has been reset to default values.\n");
}

int curve_interpolate(int temp, const FanCurve *curve) {
    if (curve->point_count == 0)
        return 30;

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

int curve_default_interpolate(int temp) {
    if (temp < 30) return 30;
    if (temp >= 75) return 100;

    int temps[]  = {30, 40, 50, 60, 70, 75};
    int speeds[] = {30, 40, 55, 70, 90, 100};
    int n = 6;

    for (int i = 0; i < n - 1; i++) {
        if (temp >= temps[i] && temp < temps[i + 1]) {
            float slope = (float)(speeds[i + 1] - speeds[i]) /
                          (float)(temps[i + 1] - temps[i]);
            return speeds[i] + (int)(slope * (float)(temp - temps[i]));
        }
    }
    return 100;
}
