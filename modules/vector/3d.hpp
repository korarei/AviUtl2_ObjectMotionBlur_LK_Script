#pragma once

#include "2d.hpp"

template <std::floating_point T>
class Vec3;

template <std::floating_point T>
class Mat3;

template <std::floating_point T>
class Diag3;

template <std::floating_point T>
class Vec3 : public vector::Vec<Vec3<T>, 3, T> {
public:
    constexpr Vec3() noexcept : Super() {}
    constexpr Vec3(T x, T y, T z) noexcept : Super(x, y, z) {}
    explicit constexpr Vec3(const Vec2<T> &v, T z = T(0)) noexcept : Super(v.x(), v.y(), z) {}

    [[nodiscard]] constexpr T &x() noexcept { return this->vec[0]; }
    [[nodiscard]] constexpr T &y() noexcept { return this->vec[1]; }
    [[nodiscard]] constexpr T &z() noexcept { return this->vec[2]; }
    [[nodiscard]] constexpr const T &x() const noexcept { return this->vec[0]; }
    [[nodiscard]] constexpr const T &y() const noexcept { return this->vec[1]; }
    [[nodiscard]] constexpr const T &z() const noexcept { return this->vec[2]; }

    [[nodiscard]] constexpr Vec2<T> to_vec2() const noexcept { return Vec2(this->vec[0], this->vec[1]); }

    [[nodiscard]] constexpr Vec3 cross(const Vec3 &v) const noexcept {
        const T vx = y() * v.z() - z() * v.y();
        const T vy = z() * v.x() - x() * v.z();
        const T vz = x() * v.y() - y() * v.x();
        return Vec3(vx, vy, vz);
    }

private:
    using Super = vector::Vec<Vec3<T>, 3, T>;
};

template <std::floating_point T>
[[nodiscard]] constexpr Vec3<T>
operator*(T scalar, const Vec3<T> &v) noexcept {
    return v * scalar;
}

template <std::floating_point T>
class Mat3 : public vector::Mat<Vec3<T>, Mat3<T>, Diag3<T>, 3, T> {
public:
    constexpr Mat3() noexcept : Super() {}
    constexpr Mat3(const Vec3<T> &c0, const Vec3<T> &c1, const Vec3<T> &c2) noexcept : Super(c0, c1, c2) {}
    explicit constexpr Mat3(const Mat2<T> &m, const Vec3<T> &t = Vec3<T>()) noexcept :
        Super(Vec3(m[0]), Vec3(m[1]), t) {}

    [[nodiscard]] constexpr Mat2<T> to_mat2() const noexcept {
        return Mat2(this->cols[0].to_vec2(), this->cols[1].to_vec2());
    }

    [[nodiscard]] static constexpr Mat3 rotation(T theta, T scale = T(1), int axis = 2) {
        const T c = std::cos(theta) * scale;
        const T s = std::sin(theta) * scale;

        switch (axis) {
            case 0:
                return Mat3(Vec3(T(1), T(0), T(0)), Vec3(T(0), c, s), Vec3(T(0), -s, c));
            case 1:
                return Mat3(Vec3(c, T(0), -s), Vec3(T(0), T(1), T(0)), Vec3(s, T(0), c));
            case 2:
                return Mat3(Vec3(c, s, T(0)), Vec3(-s, c, T(0)), Vec3(T(0), T(0), T(1)));
            default:
                throw std::invalid_argument("Unsupported axis.");
        }
    }

private:
    using Super = vector::Mat<Vec3<T>, Mat3<T>, Diag3<T>, 3, T>;
};

template <std::floating_point T>
[[nodiscard]] constexpr Mat3<T>
operator*(T scalar, const Mat3<T> &m) noexcept {
    return m * scalar;
}

template <std::floating_point T>
class Diag3 : public vector::Diag<Vec3<T>, Mat3<T>, Diag3<T>, 3, T> {
public:
    constexpr Diag3() noexcept : Super() {}
    explicit constexpr Diag3(T s) noexcept : Super(s, s, s) {}
    constexpr Diag3(T sx, T sy, T sz) noexcept : Super(sx, sy, sz) {}
    explicit constexpr Diag3(const Vec3<T> &v) noexcept : Super(v.x(), v.y(), v.z()) {}
    explicit constexpr Diag3(const Diag2<T> &d, T sz = T(0)) noexcept : Super(d[0], d[1], sz) {}

private:
    using Super = vector::Diag<Vec3<T>, Mat3<T>, Diag3<T>, 3, T>;
};

template <std::floating_point T>
[[nodiscard]] constexpr Diag3<T>
operator*(T scalar, const Diag3<T> &d) noexcept {
    return d * scalar;
}
