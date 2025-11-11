#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

#include "utils.hpp"
#include "vector_2d.hpp"
#include "vector_3d.hpp"

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
        data{cx, cy, x, y, rz, zoom * 0.01} {}

    [[nodiscard]] constexpr double &operator[](std::size_t i) noexcept { return data[i]; }
    [[nodiscard]] constexpr const double &operator[](std::size_t i) const noexcept { return data[i]; }

    [[nodiscard]] constexpr Vec2<double> center() const noexcept { return Vec2<double>(data[0], data[1]); }
    [[nodiscard]] constexpr Vec2<double> position() const noexcept { return Vec2<double>(data[2], data[3]); }
    [[nodiscard]] constexpr double rotation() const noexcept { return to_rad(data[4]); }
    [[nodiscard]] constexpr double scale() const noexcept { return std::max(data[5], eps); }

    constexpr void set_geo(const Geo &geo) noexcept {
        for (std::size_t i = 0; i < 5; ++i) data[i] += geo[i];
        data[5] *= geo[5];
    }

private:
    static constexpr double eps = 1.0e-4;
    std::array<double, 6> data;
};

class Delta {
public:
    struct Motion {
        Mat3<double> htm;  // Homogeneous Transformation Matrix
        Vec2<double> drift;

        constexpr Motion() noexcept : htm(Mat3<double>::identity()), drift{} {}
        constexpr Motion(const Mat3<double> &m, const Vec2<double> &v) noexcept : htm(m), drift(v) {}
    };

    Delta(const Transform &from, const Transform &to) noexcept;

    [[nodiscard]] constexpr const bool is_moved() const noexcept { return !flag; }

    [[nodiscard]] Motion compute_motion(double amt, int smp = 1, bool invert = false) const noexcept;

private:
    double rot, scale;
    Vec2<double> pos, center;
    bool flag;
};
