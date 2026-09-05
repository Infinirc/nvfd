#ifndef NVFD_CURVE_H
#define NVFD_CURVE_H

#include "nvfd.h"

typedef enum {
    CURVE_OK      = 0,
    CURVE_MISSING = 1,   /* no curve file exists */
    CURVE_INVALID = -1   /* file exists but is unusable; see curve_last_error() */
} CurveStatus;

/* Loads and validates the curve file into *curve. */
CurveStatus curve_load(FanCurve *curve);
const char *curve_last_error(void);

/* Loads the curve for a caller that cannot proceed without one: reports a
 * missing or invalid file on stderr and returns -1, else 0. */
int         curve_require(FanCurve *curve);

/* A curve must not command less airflow as the die gets hotter. Returns 1 when
 * the curve rises (or is flat) throughout, else 0 and sets *bad to the index of
 * the first point that falls below its predecessor. */
int         curve_is_monotonic(const FanCurve *curve, int *bad);

int         curve_write(const FanCurve *curve);
int         curve_edit(int temp, int speed);
int         curve_reset(void);
int         curve_interpolate(int temp, const FanCurve *curve);
int         curve_apply_to_gpu(unsigned int gpu_index);

#endif /* NVFD_CURVE_H */
