#pragma once

#define _USE_MATH_DEFINES
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

inline bool
is_zero(double val) {
    constexpr double eps = 1.0e-4;
    return std::abs(val) < eps;
}

inline constexpr double
to_rad(double deg) {
    constexpr double r180 = 1.0 / 180.0;
    return deg * M_PI * r180;
}
