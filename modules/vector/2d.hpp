#pragma once

#include <cmath>

#include "base.hpp"

template <std::floating_point T>
class Vec2;

template <std::floating_point T>
class Mat2;

template <std::floating_point T>
class Diag2;

template <std::floating_point T>
class Vec2 : public vector::Vec<Vec2<T>, 2, T> {
public:
    constexpr Vec2() noexcept : Super() {}
    constexpr Vec2(T x, T y) noexcept : Super(x, y) {}

    [[nodiscard]] constexpr T &x() noexcept { return this->vec[0]; }
    [[nodiscard]] constexpr T &y() noexcept { return this->vec[1]; }
    [[nodiscard]] constexpr const T &x() const noexcept { return this->vec[0]; }
    [[nodiscard]] constexpr const T &y() const noexcept { return this->vec[1]; }

    [[nodiscard]] constexpr Vec2 rotate(T theta) const noexcept {
        const T c = std::cos(theta);
        const T s = std::sin(theta);
        return Vec2(x() * c - y() * s, x() * s + y() * c);
    }

private:
    using Super = vector::Vec<Vec2<T>, 2, T>;
};

template <std::floating_point T>
[[nodiscard]] constexpr Vec2<T>
operator*(T scalar, const Vec2<T> &v) noexcept {
    return v * scalar;
}

template <std::floating_point T>
class Mat2 : public vector::Mat<Vec2<T>, Mat2<T>, Diag2<T>, 2, T> {
public:
    constexpr Mat2() noexcept : Super() {}
    constexpr Mat2(const Vec2<T> &c0, const Vec2<T> &c1) noexcept : Super(c0, c1) {}

    [[nodiscard]] constexpr T determinant() const noexcept {
        return (*this)(0, 0) * (*this)(1, 1) - (*this)(1, 0) * (*this)(0, 1);
    }

    [[nodiscard]] static constexpr Mat2 rotation(T theta, T scale = T(1)) {
        const T c = std::cos(theta) * scale;
        const T s = std::sin(theta) * scale;
        return Mat2(Vec2(c, s), Vec2(-s, c));
    }

private:
    using Super = vector::Mat<Vec2<T>, Mat2<T>, Diag2<T>, 2, T>;
};

template <std::floating_point T>
[[nodiscard]] constexpr Mat2<T>
operator*(T scalar, const Mat2<T> &m) noexcept {
    return m * scalar;
}

template <std::floating_point T>
class Diag2 : public vector::Diag<Vec2<T>, Mat2<T>, Diag2<T>, 2, T> {
public:
    constexpr Diag2() noexcept : Super() {}
    explicit constexpr Diag2(T s) noexcept : Super(s, s) {}
    constexpr Diag2(T sx, T sy) noexcept : Super(sx, sy) {}
    explicit constexpr Diag2(const Vec2<T> &v) noexcept : Super(v.x(), v.y()) {}

private:
    using Super = vector::Diag<Vec2<T>, Mat2<T>, Diag2<T>, 2, T>;
};

template <std::floating_point T>
[[nodiscard]] constexpr Diag2<T>
operator*(T scalar, const Diag2<T> &d) noexcept {
    return d * scalar;
}
