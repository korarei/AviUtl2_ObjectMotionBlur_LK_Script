#pragma once

#ifdef _WIN32
#include <windows.h>
#else
#include <filesystem>
#endif

#include <charconv>
#include <optional>
#include <string>
#include <string_view>

namespace blur::string {
[[nodiscard]] inline std::wstring ToWString(std::u8string_view input) {
#ifdef _WIN32
    if (input.empty()) {
        return {};
    }

    const char* input_data = reinterpret_cast<const char*>(input.data());
    const int input_size = static_cast<int>(input.size());

    const int output_size = MultiByteToWideChar(CP_UTF8, 0, input_data, input_size, nullptr, 0);

    if (output_size == 0) {
        return {};
    }

    std::wstring output(output_size, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, input_data, input_size, output.data(), output_size);

    return output;
#else
    return std::filesystem::path(input).wstring();
#endif
}

[[nodiscard]] inline std::u8string ToUTF8(std::wstring_view input) {
#ifdef _WIN32
    if (input.empty()) {
        return {};
    }

    const wchar_t* input_data = input.data();
    const int input_size = static_cast<int>(input.size());

    const int output_size = WideCharToMultiByte(CP_UTF8, 0, input_data, input_size, nullptr, 0, nullptr, nullptr);

    if (output_size == 0) {
        return {};
    }

    std::u8string output(output_size, u8'\0');

    char* output_data = reinterpret_cast<char*>(output.data());

    WideCharToMultiByte(CP_UTF8, 0, input_data, input_size, output_data, output_size, nullptr, nullptr);

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
[[nodiscard]] inline std::optional<T> ToNumber(std::string_view input) noexcept {
    const char* first = input.data();
    const char* last = first + input.size();

    T output{};
    const auto [ptr, ec] = std::from_chars(first, last, output);

    return ptr == last && ec == std::errc{} ? std::optional{output} : std::nullopt;
}

template <std::integral T>
[[nodiscard]] inline std::optional<T> ToNumber(std::string_view input, int base = 10) noexcept {
    const char* first = input.data();
    const char* last = first + input.size();

    T output{};
    const auto [ptr, ec] = std::from_chars(first, last, output, base);

    return ptr == last && ec == std::errc{} ? std::optional{output} : std::nullopt;
}
}  // namespace blur::string
