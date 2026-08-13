#include "direct3d.hpp"

#include <blur.h>
#include <fullscreen.h>

namespace blur::object {
namespace {
class Direct3DErrorCategory final : public std::error_category {
  public:
    [[nodiscard]] const char* name() const noexcept override { return "direct3d"; }

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
            case Error::kCreateRenderTargetViewFailed:
                return "Failed to create render target view";
            case Error::kCreateShaderResourceViewFailed:
                return "Failed to create shader resource view";
            case Error::kCreateVertexShaderFailed:
                return "Failed to create vertex shader";
            case Error::kCreatePixelShaderFailed:
                return "Failed to create pixel shader";
            case Error::kCreateDepthStencilStateFailed:
                return "Failed to create depth stencil state";
            case Error::kCreateSamplerStateFailed:
                return "Failed to create sampler state";
        }

        return "Unknown Direct3D error";
    }
};

constinit const Direct3DErrorCategory kErrorCategory{};

[[nodiscard]] std::error_code ToErrorCode(HRESULT result, Error fallback) {
    switch (result) {
        case E_INVALIDARG:
            return std::make_error_code(std::errc::invalid_argument);
        case E_OUTOFMEMORY:
            return std::make_error_code(std::errc::not_enough_memory);
        case E_ACCESSDENIED:
            return std::make_error_code(std::errc::permission_denied);
        case E_NOTIMPL:
            return std::make_error_code(std::errc::function_not_supported);
        case DXGI_ERROR_UNSUPPORTED:
            return std::make_error_code(std::errc::not_supported);
        case DXGI_ERROR_DEVICE_REMOVED:
            return Error::kDeviceRemoved;
        case DXGI_ERROR_DEVICE_RESET:
            return Error::kDeviceReset;
        case DXGI_ERROR_DEVICE_HUNG:
            return Error::kDeviceHung;
        default:
            return fallback;
    }
}
}  // namespace

std::error_code make_error_code(Error error) noexcept { return {static_cast<int>(error), kErrorCategory}; }

std::error_code Renderer::Context::Draw(ID3D11Texture2D* src, const std::vector<Affine2D>& subframe_xforms,
                                        const Param& param) const {
    static constexpr ID3D11ShaderResourceView* null_srvs[2uz] = {nullptr, nullptr};

    HRESULT result;

    {
        D3D11_MAPPED_SUBRESOURCE mapped{};

        result = owner_.ctx_->Map(owner_.cb_.Get(), 0u, D3D11_MAP_WRITE_DISCARD, 0u, &mapped);
        if (FAILED(result)) {
            return ToErrorCode(result, Error::kMapBufferFailed);
        }

        std::memcpy(mapped.pData, &param, sizeof(param));
        owner_.ctx_->Unmap(owner_.cb_.Get(), 0u);

        owner_.ctx_->PSSetConstantBuffers(0u, 1u, owner_.cb_.GetAddressOf());
    }

    if (subframe_xforms.size() <= owner_.xforms_.capacity) {
        D3D11_MAPPED_SUBRESOURCE mapped{};

        result = owner_.ctx_->Map(owner_.xforms_.buffer.Get(), 0u, D3D11_MAP_WRITE_DISCARD, 0u, &mapped);
        if (FAILED(result)) {
            return ToErrorCode(result, Error::kMapBufferFailed);
        }

        std::memcpy(mapped.pData, subframe_xforms.data(), subframe_xforms.size() * sizeof(Affine2D));
        owner_.ctx_->Unmap(owner_.xforms_.buffer.Get(), 0u);
    } else {
        const D3D11_BUFFER_DESC desc{
            .ByteWidth = static_cast<uint32_t>(subframe_xforms.size() * sizeof(Affine2D)),
            .Usage = D3D11_USAGE_DYNAMIC,
            .BindFlags = D3D11_BIND_SHADER_RESOURCE,
            .CPUAccessFlags = D3D11_CPU_ACCESS_WRITE,
            .MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED,
            .StructureByteStride = sizeof(Affine2D),
        };

        const D3D11_SUBRESOURCE_DATA data{
            .pSysMem = subframe_xforms.data(),
            .SysMemPitch = 0u,
            .SysMemSlicePitch = 0u,
        };

        ComPtr<ID3D11Buffer> buf;
        result = owner_.device_->CreateBuffer(&desc, &data, &buf);
        if (FAILED(result)) {
            return ToErrorCode(result, Error::kCreateBufferFailed);
        }

        ComPtr<ID3D11ShaderResourceView> srv;
        result = owner_.device_->CreateShaderResourceView(buf.Get(), nullptr, &srv);
        if (FAILED(result)) {
            return ToErrorCode(result, Error::kCreateShaderResourceViewFailed);
        }

        owner_.xforms_.buffer = std::move(buf);
        owner_.xforms_.srv = std::move(srv);
        owner_.xforms_.capacity = subframe_xforms.size();
    }

    ComPtr<ID3D11ShaderResourceView> srv;
    result = owner_.device_->CreateShaderResourceView(src, nullptr, &srv);
    if (FAILED(result)) {
        return ToErrorCode(result, Error::kCreateShaderResourceViewFailed);
    }

    ID3D11ShaderResourceView* const inputs[] = {srv.Get(), owner_.xforms_.srv.Get()};

    ComPtr<ID3D11RenderTargetView> rtv;
    result = owner_.device_->CreateRenderTargetView(dst_, nullptr, &rtv);
    if (FAILED(result)) {
        return ToErrorCode(result, Error::kCreateRenderTargetViewFailed);
    }

    D3D11_TEXTURE2D_DESC desc{};
    dst_->GetDesc(&desc);

    const D3D11_VIEWPORT vp{
        .TopLeftX = 0.0f,
        .TopLeftY = 0.0f,
        .Width = static_cast<float>(desc.Width),
        .Height = static_cast<float>(desc.Height),
        .MinDepth = 0.0f,
        .MaxDepth = 1.0f,
    };

    owner_.ctx_->IASetInputLayout(nullptr);
    owner_.ctx_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    owner_.ctx_->VSSetShader(owner_.vs_.Get(), nullptr, 0u);
    owner_.ctx_->PSSetShader(owner_.ps_.Get(), nullptr, 0u);

    owner_.ctx_->PSSetShaderResources(0u, 2u, inputs);
    owner_.ctx_->PSSetSamplers(0u, 1u, owner_.smp_.GetAddressOf());

    owner_.ctx_->OMSetRenderTargets(1u, rtv.GetAddressOf(), nullptr);

    owner_.ctx_->RSSetViewports(1u, &vp);

    owner_.ctx_->Draw(3u, 0u);

    owner_.ctx_->OMSetRenderTargets(0u, nullptr, nullptr);
    owner_.ctx_->PSSetShaderResources(0u, 2u, null_srvs);

    return {};
}

std::error_code Renderer::Acquire(ID3D11Texture2D* tex) {
    {
        ComPtr<ID3D11Device> device;
        tex->GetDevice(&device);

        if (device_ != nullptr && device_ == device) {
            return {};
        }

        Release();

        device_ = std::move(device);
        device_->GetImmediateContext(&ctx_);
    }

    HRESULT result;

    {
        constexpr D3D11_DEPTH_STENCIL_DESC desc{
            .DepthEnable = FALSE,
            .DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO,
            .DepthFunc = D3D11_COMPARISON_ALWAYS,
            .StencilEnable = FALSE,
            .StencilReadMask = D3D11_DEFAULT_STENCIL_READ_MASK,
            .StencilWriteMask = D3D11_DEFAULT_STENCIL_WRITE_MASK,
            .FrontFace = {},
            .BackFace = {},
        };

        result = device_->CreateDepthStencilState(&desc, &dss_);
        if (FAILED(result)) {
            Release();
            return ToErrorCode(result, Error::kCreateDepthStencilStateFailed);
        }
    }

    result = device_->CreateVertexShader(g_fullscreen, sizeof(g_fullscreen), nullptr, &vs_);
    if (FAILED(result)) {
        Release();
        return ToErrorCode(result, Error::kCreateVertexShaderFailed);
    }

    result = device_->CreatePixelShader(g_blur, sizeof(g_blur), nullptr, &ps_);
    if (FAILED(result)) {
        Release();
        return ToErrorCode(result, Error::kCreatePixelShaderFailed);
    }

    {
        constexpr D3D11_SAMPLER_DESC desc{
            .Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR,
            .AddressU = D3D11_TEXTURE_ADDRESS_BORDER,
            .AddressV = D3D11_TEXTURE_ADDRESS_BORDER,
            .AddressW = D3D11_TEXTURE_ADDRESS_BORDER,
            .MipLODBias = 0.0f,
            .MaxAnisotropy = 0u,
            .ComparisonFunc = D3D11_COMPARISON_NEVER,
            .BorderColor = {0.0f, 0.0f, 0.0f, 0.0f},
            .MinLOD = 0.0f,
            .MaxLOD = D3D11_FLOAT32_MAX,
        };

        result = device_->CreateSamplerState(&desc, &smp_);
        if (FAILED(result)) {
            Release();
            return ToErrorCode(result, Error::kCreateSamplerStateFailed);
        }
    }

    {
        constexpr D3D11_BUFFER_DESC desc{
            .ByteWidth = sizeof(Param),
            .Usage = D3D11_USAGE_DYNAMIC,
            .BindFlags = D3D11_BIND_CONSTANT_BUFFER,
            .CPUAccessFlags = D3D11_CPU_ACCESS_WRITE,
            .MiscFlags = 0u,
            .StructureByteStride = 0u,
        };

        result = device_->CreateBuffer(&desc, nullptr, &cb_);
        if (FAILED(result)) {
            Release();
            return ToErrorCode(result, Error::kCreateBufferFailed);
        }
    }

    return {};
}

void Renderer::Reset() {
    const std::lock_guard lock(mutex_);
    Release();
}

void Renderer::Release() {
    xforms_.buffer.Reset();
    xforms_.srv.Reset();
    xforms_.capacity = 0uz;

    cb_.Reset();
    smp_.Reset();
    ps_.Reset();
    vs_.Reset();
    dss_.Reset();

    ctx_.Reset();
    device_.Reset();
}
}  // namespace blur::object
