#pragma once

#include <windows.h>

#include <cstdint>
#include <optional>
#include <system_error>

namespace blur::error::direct3d {
enum class Error : uint8_t {
    kDeviceRemoved = 1u,
    kDeviceReset,
    kDeviceHung,
    kMapBufferFailed,
    kCreateBufferFailed,
    kCreateTexture2DFailed,
    kCreateRenderTargetViewFailed,
    kCreateShaderResourceViewFailed,
    kCreateUnorderedAccessViewFailed,
    kCreateVertexShaderFailed,
    kCreatePixelShaderFailed,
    kCreateComputeShaderFailed,
    kCreateDepthStencilStateFailed,
    kCreateSamplerStateFailed,
};

[[nodiscard]] const std::error_category& Category() noexcept;

[[nodiscard]] std::error_code make_error_code(Error error) noexcept;

[[nodiscard]] std::optional<std::error_code> ToErrorCode(HRESULT result) noexcept;
}  // namespace blur::error::direct3d

template <>
struct std::is_error_code_enum<blur::error::direct3d::Error> : true_type {};

