#pragma once

#ifdef _WIN32
#include <windows.h>
#else
#include <filesystem>
#endif

#include <charconv>
#include <expected>
#include <string>
#include <string_view>
#include <system_error>

namespace blur::string {
[[nodiscard]] inline std::expected<std::wstring, std::error_code> ToWString(std::u8string_view input) {
#ifdef _WIN32
    if (input.empty()) {
        return {};
    }

    if (input.size() > static_cast<size_t>(std::numeric_limits<int>::max())) {
        return std::unexpected(std::make_error_code(std::errc::value_too_large));
    }

    const char* input_data = reinterpret_cast<const char*>(input.data());
    const int input_size = static_cast<int>(input.size());

    const int output_size = MultiByteToWideChar(CP_UTF8, 0, input_data, input_size, nullptr, 0);

    if (output_size == 0) {
        return std::unexpected(std::error_code{static_cast<int>(GetLastError()), std::system_category()});
    }

    std::wstring output(output_size, L'\0');
    const int written_size = MultiByteToWideChar(CP_UTF8, 0, input_data, input_size, output.data(), output_size);

    if (written_size == 0) {
        return std::unexpected(std::error_code{static_cast<int>(GetLastError()), std::system_category()});
    }

    output.resize(written_size);  // 一応

    return output;
#else
    return std::filesystem::path(input).wstring();
#endif
}

[[nodiscard]] inline std::expected<std::u8string, std::error_code> ToUTF8(std::wstring_view input) {
#ifdef _WIN32
    if (input.empty()) {
        return {};
    }

    if (input.size() > static_cast<size_t>(std::numeric_limits<int>::max())) {
        return std::unexpected(std::make_error_code(std::errc::value_too_large));
    }

    const wchar_t* input_data = input.data();
    const int input_size = static_cast<int>(input.size());

    const int output_size = WideCharToMultiByte(CP_UTF8, 0, input_data, input_size, nullptr, 0, nullptr, nullptr);

    if (output_size == 0) {
        return std::unexpected(std::error_code{static_cast<int>(GetLastError()), std::system_category()});
    }

    std::u8string output(output_size, u8'\0');
    const int written_size = WideCharToMultiByte(CP_UTF8, 0, input_data, input_size,
                                                 reinterpret_cast<char*>(output.data()), output_size, nullptr, nullptr);

    if (written_size == 0) {
        return std::unexpected(std::error_code{static_cast<int>(GetLastError()), std::system_category()});
    }

    output.resize(written_size);  // 一応

    return output;
#else
    return std::filesystem::path(input).u8string();
#endif
}

[[nodiscard]] inline std::string AsString(std::u8string_view input) {
    return {reinterpret_cast<const char*>(input.data()), input.size()};
}

[[nodiscard]] inline std::u8string AsUTF8(std::string_view input) {
    return {reinterpret_cast<const char8_t*>(input.data()), input.size()};
}

template <std::floating_point T>
[[nodiscard]] inline std::expected<T, std::error_code> ToNumber(std::string_view input) noexcept {
    const char* first = input.data();
    const char* last = first + input.size();

    T output{};
    const auto [ptr, ec] = std::from_chars(first, last, output);

    if (ec != std::errc{}) {
        return std::unexpected(std::make_error_code(ec));
    }

    if (ptr != last) {
        return std::unexpected(std::make_error_code(std::errc::invalid_argument));
    }

    return output;
}

template <std::floating_point T>
[[nodiscard]] inline std::expected<T, std::error_code> ToNumber(std::u8string_view input) noexcept {
    return ToNumber<T>(AsString(input));
}

template <std::integral T>
[[nodiscard]] inline std::expected<T, std::error_code> ToNumber(std::string_view input, int base = 10) noexcept {
    const char* first = input.data();
    const char* last = first + input.size();

    T output{};
    const auto [ptr, ec] = std::from_chars(first, last, output, base);

    if (ec != std::errc{}) {
        return std::unexpected(std::make_error_code(ec));
    }

    if (ptr != last) {
        return std::unexpected(std::make_error_code(std::errc::invalid_argument));
    }

    return output;
}

template <std::integral T>
[[nodiscard]] inline std::expected<T, std::error_code> ToNumber(std::u8string_view input, int base = 10) noexcept {
    return ToNumber<T>(AsString(input), base);
}
}  // namespace blur::string
