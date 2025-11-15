#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <concepts>
#include <cstddef>
#include <execution>
#include <numeric>
#include <ranges>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace vector {
template <typename V_Derived, std::size_t N, std::floating_point T>
class Vec {
public:
    constexpr Vec() = default;
    template <typename... Args>
        requires(sizeof...(Args) == N && (std::same_as<std::decay_t<Args>, T> && ...))
    constexpr Vec(Args &&...args) noexcept : vec{std::forward<Args>(args)...} {}

    [[nodiscard]] constexpr T &operator[](std::size_t i) noexcept { return vec[i]; }
    [[nodiscard]] constexpr const T &operator[](std::size_t i) const noexcept { return vec[i]; }

    [[nodiscard]] constexpr T *data() noexcept { return vec.data(); }
    [[nodiscard]] constexpr const T *data() const noexcept { return vec.data(); }

    [[nodiscard]] static constexpr std::size_t size() noexcept { return N; }

    constexpr V_Derived &operator+=(const V_Derived &other) noexcept {
        for (std::size_t i = 0; i < N; ++i) vec[i] += other[i];
        return this->derived();
    }

    constexpr V_Derived &operator-=(const V_Derived &other) noexcept {
        for (std::size_t i = 0; i < N; ++i) vec[i] -= other[i];
        return this->derived();
    }

    constexpr V_Derived &operator*=(const T &scalar) noexcept {
        for (auto &elem : vec) elem *= scalar;
        return this->derived();
    }

    constexpr V_Derived &operator*=(const V_Derived &other) noexcept {
        for (std::size_t i = 0; i < N; ++i) vec[i] *= other[i];
        return this->derived();
    }

    constexpr V_Derived &operator/=(const T &scalar) {
        for (auto &elem : vec) elem /= scalar;
        return this->derived();
    }

    constexpr V_Derived &operator/=(const V_Derived &other) {
        for (std::size_t i = 0; i < N; ++i) vec[i] /= other[i];
        return this->derived();
    }

    [[nodiscard]] constexpr V_Derived operator+(const V_Derived &other) const noexcept {
        V_Derived result = this->derived();
        result += other;
        return result;
    }

    [[nodiscard]] constexpr V_Derived operator-(const V_Derived &other) const noexcept {
        V_Derived result = this->derived();
        result -= other;
        return result;
    }

    [[nodiscard]] constexpr V_Derived operator*(const T &scalar) const noexcept {
        V_Derived result = this->derived();
        result *= scalar;
        return result;
    }

    [[nodiscard]] constexpr V_Derived operator*(const V_Derived &other) const noexcept {
        V_Derived result = this->derived();
        result *= other;
        return result;
    }

    [[nodiscard]] constexpr V_Derived operator/(const T &scalar) const {
        V_Derived result = this->derived();
        result /= scalar;
        return result;
    }

    [[nodiscard]] constexpr V_Derived operator/(const V_Derived &other) const {
        V_Derived result = this->derived();
        result /= other;
        return result;
    }

    [[nodiscard]] constexpr V_Derived operator-() const noexcept {
        V_Derived result;
        for (std::size_t i = 0; i < N; ++i) result[i] = -vec[i];
        return result;
    }

    [[nodiscard]] constexpr T norm(int ord = 2) const {
        switch (ord) {
            case 1: {
                T sum = T(0);
                for (T v : vec) sum += std::abs(v);
                return sum;
            }
            case 2: {
                T sum = T(0);
                for (T v : vec) sum += v * v;
                return std::sqrt(sum);
            }
            case -1:
                return std::ranges::max(vec | std::views::transform([](T v) { return std::abs(v); }));
            default:
                throw std::invalid_argument("unsupported norm");
        }
    }

    [[nodiscard]] constexpr T dot(const V_Derived &other) const noexcept {
        T result = T(0);
        for (std::size_t i = 0; i < N; ++i) result += vec[i] * other[i];
        return result;
    }

    [[nodiscard]] constexpr V_Derived ceil() const noexcept {
        V_Derived result;
        for (std::size_t i = 0; i < N; ++i) result[i] = std::ceil(vec[i]);
        return result;
    }

    [[nodiscard]] constexpr V_Derived floor() const noexcept {
        V_Derived result;
        for (std::size_t i = 0; i < N; ++i) result[i] = std::floor(vec[i]);
        return result;
    }

    [[nodiscard]] constexpr V_Derived abs() const noexcept {
        V_Derived result;
        for (std::size_t i = 0; i < N; ++i) result[i] = std::abs(vec[i]);
        return result;
    }

    [[nodiscard]] constexpr V_Derived max(const V_Derived &other) const noexcept {
        V_Derived result;
        for (std::size_t i = 0; i < N; ++i) result[i] = std::max(vec[i], other[i]);
        return result;
    }

    [[nodiscard]] constexpr V_Derived min(const V_Derived &other) const noexcept {
        V_Derived result;
        for (std::size_t i = 0; i < N; ++i) result[i] = std::min(vec[i], other[i]);
        return result;
    }

protected:
    std::array<T, N> vec{};

    [[nodiscard]] constexpr V_Derived &derived() noexcept { return static_cast<V_Derived &>(*this); }
    [[nodiscard]] constexpr const V_Derived &derived() const noexcept { return static_cast<const V_Derived &>(*this); }
};

template <typename V_Derived, typename M_Derived, typename D_Derived, std::size_t N, std::floating_point T>
class Mat {
public:
    constexpr Mat() = default;
    template <typename... Args>
        requires(sizeof...(Args) == N && (std::same_as<std::decay_t<Args>, V_Derived> && ...))
    constexpr Mat(Args &&...args) noexcept : cols{std::forward<Args>(args)...} {}

    [[nodiscard]] constexpr V_Derived &operator[](std::size_t col) noexcept { return cols[col]; }
    [[nodiscard]] constexpr const V_Derived &operator[](std::size_t col) const noexcept { return cols[col]; }
    [[nodiscard]] constexpr T &operator()(std::size_t i, std::size_t j) noexcept { return cols[j][i]; }
    [[nodiscard]] constexpr const T &operator()(std::size_t i, std::size_t j) const noexcept { return cols[j][i]; }

    [[nodiscard]] constexpr T *data() noexcept { return cols.data()->data(); }
    [[nodiscard]] constexpr const T *data() const noexcept { return cols.data()->data(); }

    [[nodiscard]] static constexpr std::size_t size() noexcept { return N * N; }

    constexpr M_Derived &operator+=(const M_Derived &other) noexcept {
        for (std::size_t j = 0; j < N; ++j) cols[j] += other[j];
        return this->derived();
    }

    constexpr M_Derived &operator-=(const M_Derived &other) noexcept {
        for (std::size_t j = 0; j < N; ++j) cols[j] -= other[j];
        return this->derived();
    }

    constexpr M_Derived &operator*=(const T &scalar) noexcept {
        for (auto &col : cols) col *= scalar;
        return this->derived();
    }

    constexpr M_Derived &operator/=(const T &scalar) {
        for (auto &col : cols) col /= scalar;
        return this->derived();
    }

    [[nodiscard]] constexpr M_Derived operator+(const M_Derived &other) const noexcept {
        M_Derived result = this->derived();
        result += other;
        return result;
    }

    [[nodiscard]] constexpr M_Derived operator-(const M_Derived &other) const noexcept {
        M_Derived result = this->derived();
        result -= other;
        return result;
    }

    [[nodiscard]] constexpr M_Derived operator/(const T &scalar) const {
        M_Derived result = this->derived();
        result /= scalar;
        return result;
    }

    [[nodiscard]] constexpr M_Derived operator*(const T &scalar) const noexcept {
        M_Derived result = this->derived();
        result *= scalar;
        return result;
    }

    [[nodiscard]] constexpr M_Derived operator*(const M_Derived &other) const noexcept {
        constexpr auto idx = std::views::iota(0, N);

        M_Derived result;
        std::for_each(std::execution::par_unseq, idx.begin(), idx.end(), [&](std::size_t k) {
            const auto &vec = other[k];
            for (std::size_t j = 0; j < N; ++j) {
                const T &v = vec[j];
                for (std::size_t i = 0; i < N; ++i) {
                    result[k][i] += (*this)[j][i] * v;
                }
            }
        });
        return result;
    }

    [[nodiscard]] constexpr V_Derived operator*(const V_Derived &vec) const noexcept {
        V_Derived result;
        for (std::size_t j = 0; j < N; ++j) {
            const T &v = vec[j];
            for (std::size_t i = 0; i < N; ++i) {
                result[i] += (*this)[j][i] * v;
            }
        }
        return result;
    }

    [[nodiscard]] constexpr M_Derived operator*(const D_Derived &diag) const noexcept {
        M_Derived result;
        for (std::size_t j = 0; j < N; ++j) {
            const T &v = diag[j];
            for (std::size_t i = 0; i < N; ++i) {
                result[j][i] = v * (*this)[j][i];
            }
        }
        return result;
    }

    [[nodiscard]] constexpr M_Derived transpose() const noexcept {
        M_Derived result;
        for (std::size_t j = 0; j < N; ++j) {
            for (std::size_t i = 0; i < N; ++i) {
                result[i][j] = (*this)[j][i];
            }
        }
        return result;
    }

    [[nodiscard]] constexpr M_Derived abs() const noexcept {
        M_Derived result;
        for (std::size_t i = 0; i < N; ++i) result[i] = cols[i].abs();
        return result;
    }

    [[nodiscard]] static constexpr M_Derived identity() noexcept {
        M_Derived result;
        for (std::size_t i = 0; i < N; ++i) result[i][i] = T(1);
        return result;
    }

protected:
    std::array<V_Derived, N> cols{};

    [[nodiscard]] constexpr M_Derived &derived() noexcept { return static_cast<M_Derived &>(*this); }
    [[nodiscard]] constexpr const M_Derived &derived() const noexcept { return static_cast<const M_Derived &>(*this); }
};

template <typename V_Derived, typename M_Derived, typename D_Derived, std::size_t N, std::floating_point T>
class Diag {
public:
    constexpr Diag() = default;
    template <typename... Args>
        requires(sizeof...(Args) == N && (std::same_as<std::decay_t<Args>, T> && ...))
    constexpr Diag(Args &&...args) noexcept : diag{std::forward<Args>(args)...} {}

    [[nodiscard]] constexpr T &operator[](std::size_t i) noexcept { return diag[i]; }
    [[nodiscard]] constexpr const T &operator[](std::size_t i) const noexcept { return diag[i]; }

    constexpr D_Derived &operator+=(const D_Derived &other) noexcept {
        for (std::size_t i = 0; i < N; ++i) diag[i] += other[i];
        return this->derived();
    }

    constexpr D_Derived &operator-=(const D_Derived &other) noexcept {
        for (std::size_t i = 0; i < N; ++i) diag[i] -= other[i];
        return this->derived();
    }

    constexpr D_Derived &operator*=(const T &scalar) noexcept {
        for (auto &elem : diag) elem *= scalar;
        return this->derived();
    }

    constexpr D_Derived &operator/=(const T &scalar) {
        for (auto &elem : diag) elem /= scalar;
        return this->derived();
    }

    [[nodiscard]] constexpr D_Derived operator+(const D_Derived &other) const noexcept {
        D_Derived result = this->derived();
        result += other;
        return result;
    }

    [[nodiscard]] constexpr D_Derived operator-(const D_Derived &other) const noexcept {
        D_Derived result = this->derived();
        result -= other;
        return result;
    }

    [[nodiscard]] constexpr D_Derived operator*(const T &scalar) const noexcept {
        D_Derived result = this->derived();
        result *= scalar;
        return result;
    }

    [[nodiscard]] constexpr D_Derived operator/(const T &scalar) const {
        D_Derived result = this->derived();
        result /= scalar;
        return result;
    }

    [[nodiscard]] constexpr D_Derived operator*(const D_Derived &other) const noexcept {
        D_Derived result;
        for (std::size_t i = 0; i < N; ++i) result[i] = diag[i] * other[i];
        return result;
    }

    [[nodiscard]] constexpr V_Derived operator*(const V_Derived &other) const noexcept {
        V_Derived result;
        for (std::size_t i = 0; i < N; ++i) result[i] = diag[i] * other[i];
        return result;
    }

    [[nodiscard]] constexpr M_Derived operator*(const M_Derived &other) const noexcept {
        M_Derived result;
        for (std::size_t j = 0; j < N; ++j) {
            for (std::size_t i = 0; i < N; ++i) {
                result[j][i] = diag[i] * other[j][i];
            }
        }
        return result;
    }

    [[nodiscard]] constexpr M_Derived matrix() const noexcept {
        M_Derived result;
        for (std::size_t i = 0; i < N; ++i) result[i][i] = diag[i];
        return result;
    }

    [[nodiscard]] constexpr T determinant() const noexcept {
        return std::accumulate(diag.begin(), diag.end(), T(1), std::multiplies<>{});
    }

    [[nodiscard]] constexpr D_Derived inverse() const {
        D_Derived result;
        for (std::size_t i = 0; i < N; ++i) result[i] = T(1) / diag[i];
        return result;
    }

    [[nodiscard]] constexpr D_Derived pow(T exp) const {
        D_Derived result;
        for (std::size_t i = 0; i < N; ++i) result[i] = std::pow(diag[i], exp);
        return result;
    }

    [[nodiscard]] static constexpr D_Derived identity() noexcept {
        return []<std::size_t... I>(std::index_sequence<I...>) {
            return D_Derived((static_cast<void>(I), T(1))...);
        }(std::make_index_sequence<N>{});
    }

private:
    std::array<T, N> diag{};

    [[nodiscard]] constexpr D_Derived &derived() noexcept { return static_cast<D_Derived &>(*this); }
    [[nodiscard]] constexpr const D_Derived &derived() const noexcept { return static_cast<const D_Derived &>(*this); }
};
}  // namespace vector
