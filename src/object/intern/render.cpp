#include "render.hpp"

#include <wrl/client.h>

#include <mutex>

#include <blur.h>
#include <fullscreen.h>

namespace {
template <typename T>
using ComPtr = Microsoft::WRL::ComPtr<T>;

namespace d3d {
std::mutex mutex;

ComPtr<ID3D11Device> device = nullptr;
ComPtr<ID3D11DeviceContext> ctx = nullptr;

ComPtr<ID3D11DepthStencilState> dss = nullptr;
ComPtr<ID3D11VertexShader> vs = nullptr;
ComPtr<ID3D11PixelShader> ps = nullptr;
ComPtr<ID3D11SamplerState> smp = nullptr;
ComPtr<ID3D11Buffer> cb = nullptr;

struct {
    ComPtr<ID3D11Buffer> buf = nullptr;
    ComPtr<ID3D11ShaderResourceView> srv = nullptr;
    size_t capacity = 0uz;
} xforms{};

void Release() {
    xforms.buf.Reset();
    xforms.srv.Reset();
    xforms.capacity = 0uz;

    cb.Reset();
    smp.Reset();
    ps.Reset();
    vs.Reset();
    dss.Reset();

    ctx.Reset();
    device.Reset();
}
}  // namespace d3d
}  // namespace

namespace blur::object::renderer {
namespace {
class ErrorCategory final : public std::error_category {
  public:
    [[nodiscard]] const char* name() const noexcept override { return "blur.object.renderer"; }

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

        return "Unknown error";
    }
};

[[nodiscard]] std::error_code ToErrorCode(HRESULT result, Error fallback) noexcept {
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
            return fallback;
    }
}

constinit const ErrorCategory kErrorCategory{};

[[nodiscard]] std::error_code EnsureResources(ID3D11Texture2D* tex) {
    {
        ComPtr<ID3D11Device> device;
        tex->GetDevice(&device);

        if (d3d::device != nullptr && d3d::device == device) {
            return {};
        }

        d3d::Release();

        d3d::device = std::move(device);
        d3d::device->GetImmediateContext(&d3d::ctx);
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

        result = d3d::device->CreateDepthStencilState(&desc, &d3d::dss);
        if (FAILED(result)) {
            d3d::Release();
            return ToErrorCode(result, Error::kCreateDepthStencilStateFailed);
        }
    }

    result = d3d::device->CreateVertexShader(g_fullscreen, sizeof(g_fullscreen), nullptr, &d3d::vs);
    if (FAILED(result)) {
        d3d::Release();
        return ToErrorCode(result, Error::kCreateVertexShaderFailed);
    }

    result = d3d::device->CreatePixelShader(g_blur, sizeof(g_blur), nullptr, &d3d::ps);
    if (FAILED(result)) {
        d3d::Release();
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

        result = d3d::device->CreateSamplerState(&desc, &d3d::smp);
        if (FAILED(result)) {
            d3d::Release();
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

        result = d3d::device->CreateBuffer(&desc, nullptr, &d3d::cb);
        if (FAILED(result)) {
            d3d::Release();
            return ToErrorCode(result, Error::kCreateBufferFailed);
        }
    }

    return {};
}
}  // namespace

std::error_code make_error_code(Error error) noexcept { return {static_cast<int>(error), kErrorCategory}; }

[[nodiscard]] Context CreateContext(ID3D11Texture2D* dst) { return Context(dst); }

// subframe_xforms は empty を許さず，あまりにも大きなサイズ (2 GiB?) も認めない．呼び出し側に責任．
std::error_code Context::Draw(ID3D11Texture2D* src, const std::vector<Float2x3>& subframe_xforms,
                              const Param& param) const {
    static constexpr ID3D11ShaderResourceView* null_srvs[2uz] = {nullptr, nullptr};

    HRESULT result;

    {
        D3D11_MAPPED_SUBRESOURCE mapped{};

        result = d3d::ctx->Map(d3d::cb.Get(), 0u, D3D11_MAP_WRITE_DISCARD, 0u, &mapped);
        if (FAILED(result)) {
            return ToErrorCode(result, Error::kMapBufferFailed);
        }

        std::memcpy(mapped.pData, &param, sizeof(param));
        d3d::ctx->Unmap(d3d::cb.Get(), 0u);

        d3d::ctx->PSSetConstantBuffers(0u, 1u, d3d::cb.GetAddressOf());
    }

    if (subframe_xforms.size() <= d3d::xforms.capacity) {
        D3D11_MAPPED_SUBRESOURCE mapped{};

        result = d3d::ctx->Map(d3d::xforms.buf.Get(), 0u, D3D11_MAP_WRITE_DISCARD, 0u, &mapped);
        if (FAILED(result)) {
            return ToErrorCode(result, Error::kMapBufferFailed);
        }

        std::memcpy(mapped.pData, subframe_xforms.data(), subframe_xforms.size() * sizeof(Float2x3));
        d3d::ctx->Unmap(d3d::xforms.buf.Get(), 0u);
    } else {
        const D3D11_BUFFER_DESC desc{
            .ByteWidth = static_cast<UINT>(subframe_xforms.size() * sizeof(Float2x3)),
            .Usage = D3D11_USAGE_DYNAMIC,
            .BindFlags = D3D11_BIND_SHADER_RESOURCE,
            .CPUAccessFlags = D3D11_CPU_ACCESS_WRITE,
            .MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED,
            .StructureByteStride = sizeof(Float2x3),
        };

        const D3D11_SUBRESOURCE_DATA data{
            .pSysMem = subframe_xforms.data(),
            .SysMemPitch = 0u,
            .SysMemSlicePitch = 0u,
        };

        ComPtr<ID3D11Buffer> buf;
        result = d3d::device->CreateBuffer(&desc, &data, &buf);
        if (FAILED(result)) {
            return ToErrorCode(result, Error::kCreateBufferFailed);
        }

        ComPtr<ID3D11ShaderResourceView> srv;
        result = d3d::device->CreateShaderResourceView(buf.Get(), nullptr, &srv);
        if (FAILED(result)) {
            return ToErrorCode(result, Error::kCreateShaderResourceViewFailed);
        }

        d3d::xforms.buf = std::move(buf);
        d3d::xforms.srv = std::move(srv);
        d3d::xforms.capacity = subframe_xforms.size();
    }

    ComPtr<ID3D11ShaderResourceView> srv;
    result = d3d::device->CreateShaderResourceView(src, nullptr, &srv);
    if (FAILED(result)) {
        return ToErrorCode(result, Error::kCreateShaderResourceViewFailed);
    }

    ID3D11ShaderResourceView* const inputs[] = {srv.Get(), d3d::xforms.srv.Get()};

    ComPtr<ID3D11RenderTargetView> rtv;
    result = d3d::device->CreateRenderTargetView(dst_, nullptr, &rtv);
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

    d3d::ctx->IASetInputLayout(nullptr);
    d3d::ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    d3d::ctx->VSSetShader(d3d::vs.Get(), nullptr, 0u);

    d3d::ctx->PSSetShader(d3d::ps.Get(), nullptr, 0u);
    d3d::ctx->PSSetShaderResources(0u, 2u, inputs);
    d3d::ctx->PSSetSamplers(0u, 1u, d3d::smp.GetAddressOf());

    d3d::ctx->OMSetRenderTargets(1u, rtv.GetAddressOf(), nullptr);

    d3d::ctx->RSSetViewports(1u, &vp);

    d3d::ctx->Draw(3u, 0u);

    d3d::ctx->OMSetRenderTargets(0u, nullptr, nullptr);
    d3d::ctx->PSSetShaderResources(0u, 2u, null_srvs);

    return {};
}

std::error_code Render(ID3D11Texture2D* dst, FunctionRef callback) {
    const std::lock_guard lock(d3d::mutex);

    if (const auto ec = EnsureResources(dst); ec != std::error_code{}) {
        return ec;
    }

    d3d::ctx->OMSetBlendState(nullptr, nullptr, 0xffffffffu);
    d3d::ctx->OMSetDepthStencilState(d3d::dss.Get(), 0u);

    d3d::ctx->RSSetState(nullptr);

    d3d::ctx->GSSetShader(nullptr, nullptr, 0u);

    return callback(CreateContext(dst));
}

void Reset() {
    const std::lock_guard lock(d3d::mutex);
    d3d::Release();
}
}  // namespace blur::object::renderer
