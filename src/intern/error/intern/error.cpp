#include "../error.hpp"

#include <string>

namespace blur::error::direct3d {
namespace {
class ErrorCategory final : public std::error_category {
  public:
    [[nodiscard]] const char* name() const noexcept override { return "blur.direct3d"; }

    [[nodiscard]] std::string message(int condition) const override {
        switch (static_cast<Error>(condition)) {
            case Error::kDeviceRemoved:
                return "Direct3D device was removed";
            case Error::kDeviceReset:
                return "Direct3D device was reset";
            case Error::kDeviceHung:
                return "Direct3D device stopped responding";
            case Error::kMapBufferFailed:
                return "Failed to map buffer";
            case Error::kCreateBufferFailed:
                return "Failed to create buffer";
            case Error::kCreateTexture2DFailed:
                return "Failed to create texture 2D";
            case Error::kCreateRenderTargetViewFailed:
                return "Failed to create render target view";
            case Error::kCreateShaderResourceViewFailed:
                return "Failed to create shader resource view";
            case Error::kCreateUnorderedAccessViewFailed:
                return "Failed to create unordered access view";
            case Error::kCreateVertexShaderFailed:
                return "Failed to create vertex shader";
            case Error::kCreatePixelShaderFailed:
                return "Failed to create pixel shader";
            case Error::kCreateComputeShaderFailed:
                return "Failed to create compute shader";
            case Error::kCreateDepthStencilStateFailed:
                return "Failed to create depth stencil state";
            case Error::kCreateSamplerStateFailed:
                return "Failed to create sampler state";
        }

        return "Unknown error";
    }
};

constinit const ErrorCategory kErrorCategory{};
}  // namespace

const std::error_category& Category() noexcept { return kErrorCategory; }

std::error_code make_error_code(Error error) noexcept { return {static_cast<int>(error), kErrorCategory}; }

std::optional<std::error_code> ToErrorCode(HRESULT result) noexcept {
    if (SUCCEEDED(result)) {
        return std::error_code{};
    }

    switch (result) {
        case E_INVALIDARG:
            return std::make_error_code(std::errc::invalid_argument);
        case E_OUTOFMEMORY:
            return std::make_error_code(std::errc::not_enough_memory);
        case E_ACCESSDENIED:
            return std::make_error_code(std::errc::permission_denied);
        case E_NOTIMPL:
        case E_NOINTERFACE:
            return std::make_error_code(std::errc::not_supported);
        case E_ABORT:
            return std::make_error_code(std::errc::operation_canceled);
        case DXGI_ERROR_UNSUPPORTED:
            return std::make_error_code(std::errc::not_supported);
        case DXGI_ERROR_DEVICE_REMOVED:
            return Error::kDeviceRemoved;
        case DXGI_ERROR_DEVICE_RESET:
            return Error::kDeviceReset;
        case DXGI_ERROR_DEVICE_HUNG:
            return Error::kDeviceHung;
        default:
            return std::nullopt;
    }
}
}  // namespace blur::error::direct3d
