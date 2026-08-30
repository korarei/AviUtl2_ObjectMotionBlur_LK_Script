#include "render.hpp"

#include <wrl/client.h>

#include <mutex>

#include <blur.h>
#include <fullscreen.h>

#include <intern/error/error.hpp>

namespace {
namespace renderer = blur::object::renderer;

using Microsoft::WRL::ComPtr;

namespace d3d {
using blur::error::direct3d::Error;
using blur::error::direct3d::ToErrorCode;

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
} trajectory{};
}  // namespace d3d

void Release() {
    d3d::trajectory.buf.Reset();
    d3d::trajectory.srv.Reset();
    d3d::trajectory.capacity = 0uz;

    d3d::cb.Reset();
    d3d::smp.Reset();
    d3d::ps.Reset();
    d3d::vs.Reset();
    d3d::dss.Reset();

    d3d::ctx.Reset();
    d3d::device.Reset();
}

[[nodiscard]] std::error_code EnsurePipeline(ID3D11Texture2D* tex) {
    {
        ComPtr<ID3D11Device> device;
        tex->GetDevice(&device);

        if (d3d::device != nullptr && d3d::device == device) {
            return {};
        }

        Release();

        d3d::device = std::move(device);
        d3d::device->GetImmediateContext(&d3d::ctx);
    }

#define TRY(expr, fallback)                                  \
    do {                                                     \
        if (const HRESULT hr_ = (expr); FAILED(hr_)) {       \
            Release();                                       \
            return d3d::ToErrorCode(hr_).value_or(fallback); \
        }                                                    \
    } while (false)

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

        TRY(d3d::device->CreateDepthStencilState(&desc, &d3d::dss), d3d::Error::kCreateDepthStencilStateFailed);
    }

    TRY(d3d::device->CreateVertexShader(g_fullscreen, sizeof(g_fullscreen), nullptr, &d3d::vs),
        d3d::Error::kCreateVertexShaderFailed);

    TRY(d3d::device->CreatePixelShader(g_blur, sizeof(g_blur), nullptr, &d3d::ps),
        d3d::Error::kCreatePixelShaderFailed);

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

        TRY(d3d::device->CreateSamplerState(&desc, &d3d::smp), d3d::Error::kCreateSamplerStateFailed);
    }

    {
        constexpr D3D11_BUFFER_DESC desc{
            .ByteWidth = sizeof(renderer::Parameter),
            .Usage = D3D11_USAGE_DYNAMIC,
            .BindFlags = D3D11_BIND_CONSTANT_BUFFER,
            .CPUAccessFlags = D3D11_CPU_ACCESS_WRITE,
            .MiscFlags = 0u,
            .StructureByteStride = 0u,
        };

        TRY(d3d::device->CreateBuffer(&desc, nullptr, &d3d::cb), d3d::Error::kCreateBufferFailed);
    }

#undef TRY

    return {};
}
}  // namespace

namespace blur::object::renderer {
[[nodiscard]] Context CreateContext(ID3D11Texture2D* dst) { return Context(dst); }

// subframe_xforms は empty を許さず，あまりにも大きなサイズ (2 GiB?) も認めない．呼び出し側に責任．
std::error_code Context::Draw(const Target& target, const Parameter& param) const {
    static constexpr ID3D11ShaderResourceView* null_srvs[3uz] = {nullptr, nullptr, nullptr};

    HRESULT result;

    {
        D3D11_MAPPED_SUBRESOURCE mapped{};

        result = d3d::ctx->Map(d3d::cb.Get(), 0u, D3D11_MAP_WRITE_DISCARD, 0u, &mapped);
        if (FAILED(result)) {
            return d3d::ToErrorCode(result).value_or(d3d::Error::kMapBufferFailed);
        }

        std::memcpy(mapped.pData, &param, sizeof(param));
        d3d::ctx->Unmap(d3d::cb.Get(), 0u);

        d3d::ctx->PSSetConstantBuffers(0u, 1u, d3d::cb.GetAddressOf());
    }

    if (target.trajectory.size() <= d3d::trajectory.capacity) {
        D3D11_MAPPED_SUBRESOURCE mapped{};

        result = d3d::ctx->Map(d3d::trajectory.buf.Get(), 0u, D3D11_MAP_WRITE_DISCARD, 0u, &mapped);
        if (FAILED(result)) {
            return d3d::ToErrorCode(result).value_or(d3d::Error::kMapBufferFailed);
        }

        std::memcpy(mapped.pData, target.trajectory.data(), target.trajectory.size() * sizeof(Float2x3));
        d3d::ctx->Unmap(d3d::trajectory.buf.Get(), 0u);
    } else {
        const D3D11_BUFFER_DESC desc{
            .ByteWidth = static_cast<UINT>(target.trajectory.size() * sizeof(Float2x3)),
            .Usage = D3D11_USAGE_DYNAMIC,
            .BindFlags = D3D11_BIND_SHADER_RESOURCE,
            .CPUAccessFlags = D3D11_CPU_ACCESS_WRITE,
            .MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED,
            .StructureByteStride = sizeof(Float2x3),
        };

        const D3D11_SUBRESOURCE_DATA data{
            .pSysMem = target.trajectory.data(),
            .SysMemPitch = 0u,
            .SysMemSlicePitch = 0u,
        };

        ComPtr<ID3D11Buffer> buf;
        result = d3d::device->CreateBuffer(&desc, &data, &buf);
        if (FAILED(result)) {
            return d3d::ToErrorCode(result).value_or(d3d::Error::kCreateBufferFailed);
        }

        ComPtr<ID3D11ShaderResourceView> srv;
        result = d3d::device->CreateShaderResourceView(buf.Get(), nullptr, &srv);
        if (FAILED(result)) {
            return d3d::ToErrorCode(result).value_or(d3d::Error::kCreateShaderResourceViewFailed);
        }

        d3d::trajectory.buf = std::move(buf);
        d3d::trajectory.srv = std::move(srv);
        d3d::trajectory.capacity = target.trajectory.size();
    }

    ComPtr<ID3D11ShaderResourceView> img_srv;
    result = d3d::device->CreateShaderResourceView(target.image, nullptr, &img_srv);
    if (FAILED(result)) {
        return d3d::ToErrorCode(result).value_or(d3d::Error::kCreateShaderResourceViewFailed);
    }

    ComPtr<ID3D11ShaderResourceView> map_srv;
    result = d3d::device->CreateShaderResourceView(target.map, nullptr, &map_srv);
    if (FAILED(result)) {
        return d3d::ToErrorCode(result).value_or(d3d::Error::kCreateShaderResourceViewFailed);
    }

    ID3D11ShaderResourceView* const inputs[] = {img_srv.Get(), map_srv.Get(), d3d::trajectory.srv.Get()};

    ComPtr<ID3D11RenderTargetView> rtv;
    result = d3d::device->CreateRenderTargetView(dst_, nullptr, &rtv);
    if (FAILED(result)) {
        return d3d::ToErrorCode(result).value_or(d3d::Error::kCreateRenderTargetViewFailed);
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

    d3d::ctx->PSSetShader(d3d::ps.Get(), nullptr, 0u);
    d3d::ctx->PSSetShaderResources(0u, 3u, inputs);
    d3d::ctx->PSSetSamplers(0u, 1u, d3d::smp.GetAddressOf());

    d3d::ctx->OMSetRenderTargets(1u, rtv.GetAddressOf(), nullptr);

    d3d::ctx->RSSetViewports(1u, &vp);

    d3d::ctx->Draw(3u, 0u);

    d3d::ctx->OMSetRenderTargets(0u, nullptr, nullptr);
    d3d::ctx->PSSetShaderResources(0u, 3u, null_srvs);

    return {};
}

std::error_code Render(ID3D11Texture2D* dst, FunctionRef callback) {
    const std::lock_guard lock(d3d::mutex);

    std::error_code ec;

    if (ec = EnsurePipeline(dst); ec != std::error_code{}) {
        return ec;
    }

    d3d::ctx->OMSetBlendState(nullptr, nullptr, 0xffffffffu);
    d3d::ctx->OMSetDepthStencilState(d3d::dss.Get(), 0u);

    d3d::ctx->RSSetState(nullptr);

    d3d::ctx->GSSetShader(nullptr, nullptr, 0u);

    d3d::ctx->IASetInputLayout(nullptr);
    d3d::ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    d3d::ctx->VSSetShader(d3d::vs.Get(), nullptr, 0u);

    if (ec = callback(CreateContext(dst)); ec != std::error_code{}) {
        Release();
        return ec;
    }

    return {};
}

void Reset() {
    const std::lock_guard lock(d3d::mutex);
    Release();
}
}  // namespace blur::object::renderer
