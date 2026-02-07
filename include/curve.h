#ifndef NVFD_CURVE_H
#define NVFD_CURVE_H

#include "nvfd.h"

FanCurve *curve_read(void);
int       curve_write(const FanCurve *curve);
void      curve_edit(int temp, int speed);
void      curve_reset(void);
int       curve_interpolate(int temp, const FanCurve *curve);

/* Default built-in curve interpolation (fallback when no curve file exists) */
int       curve_default_interpolate(int temp);

#endif /* NVFD_CURVE_H */
