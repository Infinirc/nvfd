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

/* Convenience wrapper: a malloc'd curve, or NULL when the file is missing or
 * invalid. An invalid file is reported on stderr. */
FanCurve   *curve_read(void);

int         curve_write(const FanCurve *curve);
int         curve_edit(int temp, int speed);
int         curve_reset(void);
int         curve_interpolate(int temp, const FanCurve *curve);
int         curve_apply_to_gpu(unsigned int gpu_index);

#endif /* NVFD_CURVE_H */
