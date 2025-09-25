#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

#include "utils.hpp"
#include "vector_2d.hpp"
#include "vector_3d.hpp"

#define ZOOM_MIN 1.0e-4f

class Geo {
public:
    constexpr Geo() noexcept : flag(false), frame(0), data{} {}
    constexpr Geo(bool flag_, std::size_t f, const std::array<float, 6> geo) noexcept :
        flag(flag_), frame(f), data(geo) {}

    [[nodiscard]] constexpr float &operator[](std::size_t i) noexcept { return data[i]; }
    [[nodiscard]] constexpr const float &operator[](std::size_t i) const noexcept { return data[i]; }

    [[nodiscard]] constexpr Geo operator+(const Geo &other) noexcept {
        Geo result = *this;
        for (std::size_t i = 0; i < 6; ++i) result[i] += other[i];
        return result;
    }

    [[nodiscard]] constexpr Geo operator-(const Geo &other) noexcept {
        Geo result = *this;
        for (std::size_t i = 0; i < 6; ++i) result[i] -= other[i];
        return result;
    }

    [[nodiscard]] constexpr Geo operator*(const float &scalar) noexcept {
        Geo result = *this;
        for (std::size_t i = 0; i < 6; ++i) result[i] *= scalar;
        return result;
    }

    [[nodiscard]] constexpr Vec2<float> get_center() const noexcept { return Vec2<float>(data[0], data[1]); }
    [[nodiscard]] constexpr Vec2<float> get_pos() const noexcept { return Vec2<float>(data[2], data[3]); }
    [[nodiscard]] constexpr float get_rot() const noexcept { return to_rad(data[4]); }
    [[nodiscard]] constexpr float get_zoom() const noexcept { return std::max(data[5], ZOOM_MIN); }

    constexpr void set_state(bool flag_, std::size_t frame_) noexcept {
        flag = flag_;
        frame = frame_;
    }

    [[nodiscard]] constexpr bool is_cached(const Geo &geo) const noexcept {
        return flag == geo.flag && frame == geo.frame;
    }

    [[nodiscard]] constexpr bool is_valid() const noexcept { return flag; }

private:
    bool flag;
    std::size_t frame;
    std::array<float, 6> data;
};

class Transform {
public:
    constexpr Transform() noexcept : data{} {}
    constexpr Transform(const std::array<float, 6> &tf) noexcept : data(tf) {}

    [[nodiscard]] constexpr float &operator[](std::size_t i) noexcept { return data[i]; }
    [[nodiscard]] constexpr const float &operator[](std::size_t i) const noexcept { return data[i]; }

    [[nodiscard]] constexpr Vec2<float> get_center() const noexcept { return Vec2<float>(data[0], data[1]); }
    [[nodiscard]] constexpr Vec2<float> get_pos() const noexcept { return Vec2<float>(data[2], data[3]); }
    [[nodiscard]] constexpr float get_rot() const noexcept { return to_rad(data[4]); }
    [[nodiscard]] constexpr float get_zoom() const noexcept { return std::max(data[5], ZOOM_MIN); }

    constexpr void set_geo(const Geo &geo) noexcept {
        for (std::size_t i = 0; i < 5; ++i) data[i] += geo[i];
        data[5] *= geo[5];
    }

private:
    std::array<float, 6> data;
};

class Delta {
public:
    // Constructors.
    Delta(const Transform &from, const Transform &to) noexcept;

    // Getters.
    [[nodiscard]] constexpr const float get_rot() const noexcept { return rel_rot; }
    [[nodiscard]] constexpr const float get_scale() const noexcept { return rel_scale; }
    [[nodiscard]] constexpr const Vec2<float> &get_pos() const noexcept { return rel_pos; }
    [[nodiscard]] constexpr const bool is_moved() const noexcept { return !flag; }

    // Calculate the HTM (Homogeneous Transformation Matrix).
    [[nodiscard]] Mat3<float> calc_htm(float amt = 1.0f, std::uint32_t smp = 1u, bool is_inv = false) const noexcept;

    [[nodiscard]] constexpr Vec2<float> calc_drift(float amt = 1.0f, std::uint32_t smp = 1u) const noexcept;

private:
    float rel_rot, rel_scale;
    Vec2<float> rel_pos, rel_center;
    bool flag;
};

inline constexpr Vec2<float>
Delta::calc_drift(float amt, std::uint32_t smp) const noexcept {
    float step_amt = smp > 1u ? amt / static_cast<float>(smp) : amt;
    return rel_center * step_amt;
}
