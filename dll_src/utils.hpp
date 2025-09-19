#pragma once

#define _USE_MATH_DEFINES
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

inline bool
is_zero(float val) {
    constexpr float epsilon = 1.0e-4f;
    return std::fabsf(val) <= epsilon;
}

inline bool
are_equal(float a, float b) {
    constexpr float epsilon = 1.0e-4f;
    return std::fabsf(a - b) <= epsilon;
}

inline constexpr float
to_rad(float deg) {
    constexpr float inv_180 = 1.0f / 180.0f;
    return deg * static_cast<float>(M_PI) * inv_180;
}
