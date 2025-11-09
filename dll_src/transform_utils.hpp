#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

#include "utils.hpp"
#include "vector_2d.hpp"
#include "vector_3d.hpp"

#define ZOOM_MIN 1.0e-4

class Geo {
public:
    constexpr Geo() noexcept : data{}, frame(0), flag(false) {}
    constexpr Geo(double cx, double cy, double ox, double oy, double rz, double zoom, int frame_) noexcept :
        data{cx, cy, ox, oy, rz, zoom}, frame(frame_), flag(true) {}

    [[nodiscard]] constexpr double &operator[](std::size_t i) noexcept { return data[i]; }
    [[nodiscard]] constexpr const double &operator[](std::size_t i) const noexcept { return data[i]; }

    [[nodiscard]] constexpr Geo operator+(const Geo &other) const noexcept {
        Geo result = *this;
        for (std::size_t i = 0; i < 6; ++i) result[i] += other[i];
        return result;
    }

    [[nodiscard]] constexpr Geo operator-(const Geo &other) const noexcept {
        Geo result = *this;
        for (std::size_t i = 0; i < 6; ++i) result[i] -= other[i];
        return result;
    }

    [[nodiscard]] constexpr Geo operator*(const double &scalar) const noexcept {
        Geo result = *this;
        for (std::size_t i = 0; i < 6; ++i) result[i] *= scalar;
        return result;
    }

    [[nodiscard]] constexpr Vec2<double> get_center() const noexcept { return Vec2<double>(data[0], data[1]); }
    [[nodiscard]] constexpr Vec2<double> get_pos() const noexcept { return Vec2<double>(data[2], data[3]); }
    [[nodiscard]] constexpr double get_rot() const noexcept { return to_rad(data[4]); }
    [[nodiscard]] constexpr double get_zoom() const noexcept { return std::max(data[5], ZOOM_MIN); }

    [[nodiscard]] constexpr bool is_cached(const Geo &geo) const noexcept {
        return flag == geo.flag && frame == geo.frame;
    }

    [[nodiscard]] constexpr bool is_valid() const noexcept { return flag; }

private:
    std::array<double, 6> data;
    int frame;
    bool flag;
};

class Transform {
public:
    constexpr Transform() noexcept : data{} {}
    constexpr Transform(double cx, double cy, double x, double y, double rz, double zoom) noexcept :
        data{cx, cy, x, y, rz, zoom} {}

    [[nodiscard]] constexpr double &operator[](std::size_t i) noexcept { return data[i]; }
    [[nodiscard]] constexpr const double &operator[](std::size_t i) const noexcept { return data[i]; }

    [[nodiscard]] constexpr Vec2<double> get_center() const noexcept { return Vec2<double>(data[0], data[1]); }
    [[nodiscard]] constexpr Vec2<double> get_pos() const noexcept { return Vec2<double>(data[2], data[3]); }
    [[nodiscard]] constexpr double get_rot() const noexcept { return to_rad(data[4]); }
    [[nodiscard]] constexpr double get_zoom() const noexcept { return std::max(data[5], ZOOM_MIN); }

    constexpr void set_geo(const Geo &geo) noexcept {
        for (std::size_t i = 0; i < 5; ++i) data[i] += geo[i];
        data[5] *= geo[5];
    }

private:
    std::array<double, 6> data;
};

class Delta {
public:
    // Constructors.
    Delta(const Transform &from, const Transform &to) noexcept;

    // Getters.
    [[nodiscard]] constexpr const double get_rot() const noexcept { return rel_rot; }
    [[nodiscard]] constexpr const double get_scale() const noexcept { return rel_scale; }
    [[nodiscard]] constexpr const Vec2<double> &get_pos() const noexcept { return rel_pos; }
    [[nodiscard]] constexpr const bool is_moved() const noexcept { return !flag; }

    // Calculate the HTM (Homogeneous Transformation Matrix).
    [[nodiscard]] Mat3<double> calc_htm(double amt = 1.0, int smp = 1, bool is_inv = false) const noexcept;

    [[nodiscard]] constexpr Vec2<double> calc_drift(double amt = 1.0, int smp = 1) const noexcept;

private:
    double rel_rot, rel_scale;
    Vec2<double> rel_pos, rel_center;
    bool flag;
};

inline constexpr Vec2<double>
Delta::calc_drift(double amt, int smp) const noexcept {
    double step_amt = smp > 1 ? amt / static_cast<double>(smp) : amt;
    return rel_center * step_amt;
}
