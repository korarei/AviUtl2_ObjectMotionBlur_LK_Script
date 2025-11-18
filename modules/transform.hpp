#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

#include "utils.hpp"
#include "vector/vector.hpp"

class Geo {
public:
    constexpr Geo() noexcept : flag(0), frame(0), data{} {}
    constexpr Geo(int frame_, double cx, double cy, double ox, double oy, double rz, double sx, double sy) noexcept :
        flag(1), frame(frame_), data{cx, cy, ox, oy, rz, sx, sy} {}

    [[nodiscard]] constexpr double &operator[](std::size_t i) noexcept { return data[i]; }
    [[nodiscard]] constexpr const double &operator[](std::size_t i) const noexcept { return data[i]; }

    [[nodiscard]] constexpr Geo operator+(const Geo &other) const noexcept {
        Geo result = *this;
        for (std::size_t i = 0; i < data.size(); ++i) result[i] += other[i];
        return result;
    }

    [[nodiscard]] constexpr Geo operator-(const Geo &other) const noexcept {
        Geo result = *this;
        for (std::size_t i = 0; i < data.size(); ++i) result[i] -= other[i];
        return result;
    }

    [[nodiscard]] constexpr Geo operator*(const double &scalar) const noexcept {
        Geo result = *this;
        for (std::size_t i = 0; i < data.size(); ++i) result[i] *= scalar;
        return result;
    }

    [[nodiscard]] constexpr bool is_cached(const Geo &geo) const noexcept {
        return flag == geo.flag && frame == geo.frame;
    }

    [[nodiscard]] constexpr bool is_valid() const noexcept { return flag; }

private:
    std::int32_t flag;
    std::int32_t frame;
    std::array<double, 7> data;
};

class Transform {
public:
    constexpr Transform() noexcept : data{} {}
    constexpr Transform(double cx, double cy, double x, double y, double rz, double sx, double sy) noexcept :
        data{cx, cy, x, y, rz, sx, sy} {}

    [[nodiscard]] constexpr double &operator[](std::size_t i) noexcept { return data[i]; }
    [[nodiscard]] constexpr const double &operator[](std::size_t i) const noexcept { return data[i]; }

    [[nodiscard]] constexpr Vec2<double> center() const noexcept { return Vec2(data[0], data[1]); }
    [[nodiscard]] constexpr Vec2<double> position() const noexcept { return Vec2(data[2], data[3]); }
    [[nodiscard]] constexpr double rotation() const noexcept { return to_rad(data[4]); }
    [[nodiscard]] constexpr Diag2<double> scale() const noexcept {
        return Diag2(std::max(data[5], eps), std::max(data[6], eps));
    }

    constexpr void set_geo(const Geo &geo) noexcept {
        for (std::size_t i = 0; i < 5; ++i) data[i] += geo[i];
        data[5] *= geo[5];
        data[6] *= geo[6];
    }

private:
    static constexpr double eps = 1.0e-4;
    std::array<double, 7> data;
};

class Delta {
public:
    struct Motion {
        Mat3<double> xform;
        Diag3<double> scale;
        Vec3<double> drift;
    };

    Delta(const Transform &from, const Transform &to) noexcept;

    [[nodiscard]] constexpr bool is_moved() const noexcept { return !flag; }

    [[nodiscard]] Motion build_xform(double amt, int smp = 1, bool inverse = false) const noexcept;

private:
    Diag2<double> base;
    Diag2<double> scale;
    Vec2<double> pos, center;
    double rot;
    bool flag;
};
