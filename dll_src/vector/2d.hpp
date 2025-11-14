#pragma once

#include <cmath>
#include <concepts>
#include <ostream>
#include <stdexcept>

#include "base.hpp"

// Define a 2D vector class template.
template <vec_base::Arithmetic T>
class Vec2 : public vec_base::VecBase<Vec2<T>, 2, T> {
public:
    // Constructors.
    constexpr Vec2() noexcept : Super() {}
    constexpr Vec2(T x, T y) noexcept : Super(x, y) {}
    constexpr Vec2(const Vec2 &other) noexcept = default;
    constexpr Vec2(Vec2 &&other) noexcept = default;

    // Assignment operators.
    constexpr Vec2 &operator=(const Vec2 &other) noexcept = default;
    constexpr Vec2 &operator=(Vec2 &&other) noexcept = default;

    // Type conversion operator.
    template <vec_base::Arithmetic U>
    [[nodiscard]] explicit constexpr operator Vec2<U>() const noexcept {
        return Vec2<U>(static_cast<U>(get_x()), static_cast<U>(get_y()));
    }

    // Getters.
    [[nodiscard]] constexpr const T &get_x() const noexcept { return this->data[0]; }
    [[nodiscard]] constexpr const T &get_y() const noexcept { return this->data[1]; }

    // Setters.
    constexpr void set_x(T new_x) noexcept { this->data[0] = new_x; }
    constexpr void set_y(T new_y) noexcept { this->data[1] = new_y; }

    // Rotation function.
    // The rotation is performed around the origin (0, 0) using the standard 2D rotation matrix.
    [[nodiscard]] constexpr Vec2 rotate(T theta = T{0}, T scale = T{1}) const noexcept
        requires std::floating_point<T>
    {
        if (this->is_zero(theta))
            return Vec2(scale * get_x(), scale * get_y());

        T cos_t = std::cos(theta);
        T sin_t = std::sin(theta);
        return Vec2(scale * (get_x() * cos_t - get_y() * sin_t), scale * (get_x() * sin_t + get_y() * cos_t));
    }

private:
    using Super = vec_base::VecBase<Vec2<T>, 2, T>;
};

// Overload the multiplication operator for scalar multiplication.
template <vec_base::Arithmetic T>
[[nodiscard]] constexpr Vec2<T>
operator*(T scalar, const Vec2<T> &vec) noexcept {
    return vec * scalar;
}

// Overload the output stream operator for Vec2.
template <vec_base::Arithmetic T>
std::ostream &
operator<<(std::ostream &os, const Vec2<T> &vec) {
    os << "Vec2(" << vec.get_x() << ", " << vec.get_y() << ")";
    return os;
}

// Define a 2x2 matrix class using Vec2.
template <vec_base::Arithmetic T>
class Mat2 : public vec_base::MatBase<Mat2<T>, Vec2<T>, 2, T> {
public:
    // Constructors.
    constexpr Mat2() noexcept : Super() {}
    constexpr Mat2(const Vec2<T> &c0, const Vec2<T> &c1) noexcept : Super(c0, c1) {}
    constexpr Mat2(T a11, T a12, T a21, T a22) noexcept : Super(Vec2<T>(a11, a21), Vec2<T>(a12, a22)) {}
    constexpr Mat2(const Mat2 &other) noexcept = default;
    constexpr Mat2(Mat2 &&other) noexcept = default;

    // Assignment operators.
    constexpr Mat2 &operator=(const Mat2 &other) noexcept = default;
    constexpr Mat2 &operator=(Mat2 &&other) noexcept = default;

    // Calculate the determinant.
    [[nodiscard]] constexpr T determinant() const noexcept {
        return (*this)(0, 0) * (*this)(1, 1) - (*this)(1, 0) * (*this)(0, 1);
    }

    // Calculate the inverse of the matrix.
    [[nodiscard]] constexpr Mat2 inverse() const {
        T det = determinant();
        if (Vec2<T>::is_zero(det))
            throw std::runtime_error("Matrix is singular and cannot be inverted.");

        T inv_det = T{1} / det;
        return Mat2((*this)(1, 1), -(*this)(0, 1), -(*this)(1, 0), (*this)(0, 0)) * inv_det;
    }

    // Create identity matrix.
    [[nodiscard]] static constexpr Mat2 identity() { return Mat2(T{1}, T{0}, T{0}, T{1}); }

    // Create rotation matrix.
    [[nodiscard]] static constexpr Mat2 rotation(T theta, T scale = T{1})
        requires std::floating_point<T>
    {
        T c = std::cos(theta) * scale;
        T s = std::sin(theta) * scale;
        return Mat2(c, -s, s, c);
    }

private:
    using Super = vec_base::MatBase<Mat2<T>, Vec2<T>, 2, T>;
};

// Overload the multiplication operator for scalar multiplication of Mat2.
template <vec_base::Arithmetic T>
[[nodiscard]] constexpr Mat2<T>
operator*(T scalar, const Mat2<T> &m) noexcept {
    return m * scalar;
}
