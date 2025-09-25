#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <concepts>
#include <cstddef>
#include <limits>
#include <ranges>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace vec_base {
template <typename T>
concept Arithmetic = std::integral<T> || std::floating_point<T>;

// Vector class.
template <typename Derived, std::size_t N, Arithmetic T>
    requires(N > 0)
class VecBase {
public:
    using Iterator = typename std::array<T, N>::iterator;
    using ConstIterator = typename std::array<T, N>::const_iterator;

    // Constructors.
    constexpr VecBase() = default;
    template <typename... Args>
    constexpr VecBase(Args &&...args) noexcept
        requires(sizeof...(Args) == N && (std::is_same_v<T, std::decay_t<Args>> && ...))
        : data{std::forward<Args>(args)...} {}

    // Element access.
    [[nodiscard]] constexpr T &operator[](std::size_t i) noexcept { return data[i]; }
    [[nodiscard]] constexpr const T &operator[](std::size_t i) const noexcept { return data[i]; }

    // Iterator support.
    [[nodiscard]] constexpr Iterator begin() noexcept { return data.begin(); }
    [[nodiscard]] constexpr ConstIterator begin() const noexcept { return data.begin(); }
    [[nodiscard]] constexpr ConstIterator cbegin() const noexcept { return data.cbegin(); }
    [[nodiscard]] constexpr Iterator end() noexcept { return data.end(); }
    [[nodiscard]] constexpr ConstIterator end() const noexcept { return data.end(); }
    [[nodiscard]] constexpr ConstIterator cend() const noexcept { return data.cend(); }

    // Size and capacity.
    [[nodiscard]] static constexpr std::size_t size() noexcept { return N; }

    // Assignment operators (return Derived&).
    constexpr Derived &operator+=(const Derived &other) noexcept {
        for (std::size_t i = 0; i < N; ++i) {
            data[i] += other[i];
        }
        return derived();
    }
    constexpr Derived &operator-=(const Derived &other) noexcept {
        for (std::size_t i = 0; i < N; ++i) {
            data[i] -= other[i];
        }
        return derived();
    }
    constexpr Derived &operator*=(const T &scalar) noexcept {
        for (auto &elem : data) {
            elem *= scalar;
        }
        return derived();
    }
    constexpr Derived &operator/=(const T &scalar) {
        if (is_zero(scalar))
            throw std::invalid_argument("Division by zero in Vec.");

        for (auto &elem : data) {
            elem /= scalar;
        }
        return derived();
    }

    // Arithmetic operators (return Derived).
    [[nodiscard]] constexpr Derived operator+(const Derived &other) const noexcept {
        Derived result = derived();
        result += other;
        return result;
    }
    [[nodiscard]] constexpr Derived operator-(const Derived &other) const noexcept {
        Derived result = derived();
        result -= other;
        return result;
    }
    [[nodiscard]] constexpr Derived operator*(const T &scalar) const noexcept {
        Derived result = derived();
        result *= scalar;
        return result;
    }
    [[nodiscard]] constexpr Derived operator/(const T &scalar) const {
        Derived result = derived();
        result /= scalar;
        return result;
    }

    // Unary operators.
    [[nodiscard]] constexpr Derived operator-() const noexcept {
        Derived result;
        for (std::size_t i = 0; i < N; ++i) {
            result[i] = -data[i];
        }
        return result;
    }

    // Comparison operators.
    [[nodiscard]] constexpr bool operator==(const Derived &other) const noexcept {
        return std::ranges::equal(data, other.data, [](const T &a, const T &b) { return are_equal(a, b); });
    }

    [[nodiscard]] constexpr bool operator!=(const Derived &other) const noexcept { return !(*this == other); }

    // Mathematical operations.
    [[nodiscard]] constexpr T norm(int ord = 2) const {
        switch (ord) {
            case 1: {
                T sum = T{0};
                for (const auto &val : data) {
                    sum += std::abs(val);
                }
                return sum;
            }
            case 2: {
                T sum = T{0};
                for (const auto &val : data) {
                    sum += val * val;
                }
                return calc_sqrt(sum);
            }
            case -1: {
                T max_val = T{};
                for (const auto &val : data) {
                    max_val = std::max(max_val, std::abs(val));
                }
                return max_val;
            }
            default:
                throw std::invalid_argument("Unsupported norm order.");
        }
    }

    [[nodiscard]] constexpr T dot(const Derived &other) const noexcept {
        T result = T{};
        for (std::size_t i = 0; i < N; ++i) {
            result += data[i] * other[i];
        }
        return result;
    }

    // Element-wise functions.
    [[nodiscard]] constexpr Derived ceil() const noexcept
        requires std::floating_point<T>
    {
        Derived result;
        for (std::size_t i = 0; i < N; ++i) {
            result[i] = std::ceil(data[i]);
        }
        return result;
    }

    [[nodiscard]] constexpr Derived floor() const noexcept
        requires std::floating_point<T>
    {
        Derived result;
        for (std::size_t i = 0; i < N; ++i) {
            result[i] = std::floor(data[i]);
        }
        return result;
    }

    [[nodiscard]] constexpr Derived abs() const noexcept {
        Derived result;
        for (std::size_t i = 0; i < N; ++i) {
            result[i] = std::abs(data[i]);
        }
        return result;
    }

    [[nodiscard]] constexpr Derived max(const Derived &other) const noexcept {
        Derived result;
        for (std::size_t i = 0; i < N; ++i) {
            result[i] = std::max(data[i], other[i]);
        }
        return result;
    }

    [[nodiscard]] constexpr Derived min(const Derived &other) const noexcept {
        Derived result;
        for (std::size_t i = 0; i < N; ++i) {
            result[i] = std::min(data[i], other[i]);
        }
        return result;
    }

    // Helper functions.
    [[nodiscard]] static constexpr bool is_zero(const T &val) noexcept {
        if constexpr (std::is_floating_point_v<T>) {
            return std::abs(val) <= epsilon();
        } else {
            return val == T{0};
        }
    }

    [[nodiscard]] static constexpr bool are_equal(const T &a, const T &b) noexcept {
        if constexpr (std::is_floating_point_v<T>) {
            return std::abs(a - b) <= epsilon();
        } else {
            return a == b;
        }
    }

protected:
    std::array<T, N> data{};

    // CRTP helper to get derived instance.
    [[nodiscard]] constexpr Derived &derived() noexcept { return static_cast<Derived &>(*this); }
    [[nodiscard]] constexpr const Derived &derived() const noexcept { return static_cast<const Derived &>(*this); }

    // Helper functions.
    [[nodiscard]] static constexpr T epsilon() noexcept {
        if constexpr (std::is_floating_point_v<T>) {
            return std::numeric_limits<T>::epsilon() * T{1000};
        } else {
            return T{0};
        }
    }

    [[nodiscard]] static constexpr T calc_sqrt(const T &value) {
        if constexpr (std::is_floating_point_v<T>) {
            return std::sqrt(value);
        } else {
            return static_cast<T>(std::sqrt(static_cast<double>(value)));
        }
    }
};

// Square matrix class.
template <typename Derived, typename VecType, std::size_t N, Arithmetic T>
    requires(N > 0) && (VecType::size() == N)
class MatBase {
public:
    // Constructors.
    constexpr MatBase() = default;
    template <typename... Args>
    constexpr MatBase(Args &&...args) noexcept
        requires(sizeof...(Args) == N && (std::is_same_v<VecType, std::decay_t<Args>> && ...))
        : cols{std::forward<Args>(args)...} {}

    // Element access.
    [[nodiscard]] constexpr VecType &operator[](std::size_t col) noexcept { return cols[col]; }
    [[nodiscard]] constexpr const VecType &operator[](std::size_t col) const noexcept { return cols[col]; }
    [[nodiscard]] constexpr T &operator()(std::size_t col, std::size_t row) noexcept { return cols[col][row]; }
    [[nodiscard]] constexpr const T &operator()(std::size_t col, std::size_t row) const noexcept {
        return cols[col][row];
    }

    // Assignment operators (return Derived&).
    constexpr Derived &operator+=(const Derived &other) noexcept {
        for (std::size_t j = 0; j < N; ++j) {
            cols[j] += other[j];
        }
        return derived();
    }

    constexpr Derived &operator-=(const Derived &other) noexcept {
        for (std::size_t j = 0; j < N; ++j) {
            cols[j] -= other[j];
        }
        return derived();
    }

    constexpr Derived &operator/=(const T &scalar) {
        if (VecType::is_zero(scalar))
            throw std::invalid_argument("Division by zero in Mat.");

        for (auto &col : cols) {
            col /= scalar;
        }
        return derived();
    }

    constexpr Derived &operator*=(const T &scalar) noexcept {
        for (auto &col : cols) {
            col *= scalar;
        }
        return derived();
    }

    // Arithmetic operators (return Derived).
    [[nodiscard]] constexpr Derived operator+(const Derived &other) const noexcept {
        Derived result = derived();
        result += other;
        return result;
    }

    [[nodiscard]] constexpr Derived operator-(const Derived &other) const noexcept {
        Derived result = derived();
        result -= other;
        return result;
    }

    [[nodiscard]] constexpr Derived operator/(const T &scalar) const {
        Derived result = derived();
        result /= scalar;
        return result;
    }

    [[nodiscard]] constexpr Derived operator*(const T &scalar) const noexcept {
        Derived result = derived();
        result *= scalar;
        return result;
    }

    [[nodiscard]] constexpr Derived operator*(const Derived &other) const noexcept {
        Derived result;
        for (std::size_t j = 0; j < N; ++j) {
            for (std::size_t i = 0; i < N; ++i) {
                T sum = T{0};
                for (std::size_t k = 0; k < N; ++k) {
                    sum += (*this)(k, i) * other(j, k);
                }
                result(j, i) = sum;
            }
        }
        return result;
    }

    [[nodiscard]] constexpr VecType operator*(const VecType &vec) const noexcept {
        VecType result;
        for (std::size_t i = 0; i < N; ++i) {
            T sum = T{0};
            for (std::size_t j = 0; j < N; ++j) {
                sum += (*this)(j, i) * vec[j];
            }
            result[i] = sum;
        }
        return result;
    }

    // Getters.
    [[nodiscard]] constexpr const T &get_elem(std::size_t row, std::size_t col) const {
        if (col >= N || row >= VecType::size())
            throw std::out_of_range("Mat element indices out of range.");

        return (*this)(col, row);
    }

    [[nodiscard]] constexpr const VecType &get_col(std::size_t idx) const {
        if (idx >= N)
            throw std::out_of_range("Mat column index out of range.");

        return cols[idx];
    }

    [[nodiscard]] constexpr VecType get_row(std::size_t idx) const {
        if (idx >= VecType::size())
            throw std::out_of_range("Mat row index out of range.");

        VecType vec;
        for (std::size_t j = 0; j < N; ++j) {
            vec[j] = (*this)(j, idx);
        }
        return vec;
    }

    // Setters.
    constexpr void set_elem(std::size_t row, std::size_t col, T val) {
        if (col >= N || row >= VecType::size())
            throw std::out_of_range("Mat element indices out of range.");

        (*this)(col, row) = val;
    }

    constexpr void set_col(std::size_t idx, const VecType &col) {
        if (idx >= N)
            throw std::out_of_range("Mat column index out of range.");

        cols[idx] = col;
    }

    constexpr void set_row(std::size_t idx, const VecType &row) {
        if (idx >= VecType::size())
            throw std::out_of_range("Mat row index out of range.");

        for (std::size_t j = 0; j < N; ++j) {
            (*this)(j, idx) = row[j];
        }
    }

    // Transpose the matrix.
    [[nodiscard]] constexpr Derived transpose() const noexcept {
        Derived result;
        for (std::size_t j = 0; j < N; ++j) {
            for (std::size_t i = 0; i < N; ++i) {
                result(j, i) = (*this)(i, j);
            }
        }
        return result;
    }

    [[nodiscard]] constexpr Derived abs() const noexcept {
        Derived result;
        for (std::size_t i = 0; i < N; ++i) {
            result[i] = cols[i].abs();
        }
        return result;
    }

protected:
    std::array<VecType, N> cols{};

    // CRTP helper to get derived instance.
    [[nodiscard]] constexpr Derived &derived() noexcept { return static_cast<Derived &>(*this); }
    [[nodiscard]] constexpr const Derived &derived() const noexcept { return static_cast<const Derived &>(*this); }
};
}  // namespace vec_base
