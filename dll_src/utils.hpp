#pragma once

#include <numbers>
#include <cmath>

inline bool
is_zero(double val) {
    constexpr double eps = 1.0e-4;
    return std::abs(val) < eps;
}

inline constexpr double
to_rad(double deg) {
    constexpr double r180 = 1.0 / 180.0;
    return deg * std::numbers::pi * r180;
}
