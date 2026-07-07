#include "direct3d.hpp"

#include <blur.h>
#include <fullscreen.h>

namespace blur::object::direct3d {
Renderer::Result Renderer::Context::Draw(ID3D11Texture2D* src, const std::vector<SampleAffine>& xforms,
                                         const Param& param) const {
    static constexpr ID3D11ShaderResourceView* null_srvs[2] = {nullptr, nullptr};
    constexpr ID3D11RenderTargetView* null_rtv = nullptr;

    if (xforms.empty()) {
        return std::unexpected(L"Structured buffer is empty.");
    }

    {
        D3D11_MAPPED_SUBRESOURCE mapped{};

        if (FAILED(owner_.ctx_->Map(owner_.cb_.Get(), 0u, D3D11_MAP_WRITE_DISCARD, 0u, &mapped))) {
            return std::unexpected(L"Failed to map constant buffer");
        }

        std::memcpy(mapped.pData, &param, sizeof(param));
        owner_.ctx_->Unmap(owner_.cb_.Get(), 0u);

        owner_.ctx_->PSSetConstantBuffers(0u, 1u, owner_.cb_.GetAddressOf());
    }

    owner_.ctx_->PSSetSamplers(0u, 1u, owner_.smp_.GetAddressOf());

    ComPtr<ID3D11RenderTargetView> rtv;

    if (FAILED(owner_.device_->CreateRenderTargetView(dst_, nullptr, &rtv))) {
        return std::unexpected(L"Failed to create render target view");
    }

    owner_.ctx_->OMSetRenderTargets(1u, rtv.GetAddressOf(), nullptr);

    {
        if (xforms.size() == owner_.xforms_.size) {
            D3D11_MAPPED_SUBRESOURCE mapped{};

            if (FAILED(owner_.ctx_->Map(owner_.xforms_.buffer.Get(), 0u, D3D11_MAP_WRITE_DISCARD, 0u, &mapped))) {
                return std::unexpected(L"Failed to map structured buffer");
            }

            std::memcpy(mapped.pData, xforms.data(), xforms.size() * sizeof(SampleAffine));
            owner_.ctx_->Unmap(owner_.xforms_.buffer.Get(), 0u);
        } else {
            const D3D11_BUFFER_DESC desc{
                .ByteWidth = static_cast<uint32_t>(xforms.size() * sizeof(SampleAffine)),
                .Usage = D3D11_USAGE_DYNAMIC,
                .BindFlags = D3D11_BIND_SHADER_RESOURCE,
                .CPUAccessFlags = D3D11_CPU_ACCESS_WRITE,
                .MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED,
                .StructureByteStride = sizeof(SampleAffine),
            };

            const D3D11_SUBRESOURCE_DATA data{
                .pSysMem = xforms.data(),
                .SysMemPitch = 0u,
                .SysMemSlicePitch = 0u,
            };

            ComPtr<ID3D11Buffer> buf;

            if (FAILED(owner_.device_->CreateBuffer(&desc, &data, &buf))) {
                return std::unexpected(L"Failed to create shader resource buffer");
            }

            ComPtr<ID3D11ShaderResourceView> srv;

            if (FAILED(owner_.device_->CreateShaderResourceView(buf.Get(), nullptr, &srv))) {
                return std::unexpected(L"Failed to create shader resource view");
            }

            owner_.xforms_.buffer = std::move(buf);
            owner_.xforms_.srv = std::move(srv);
            owner_.xforms_.size = xforms.size();
        }

        ComPtr<ID3D11ShaderResourceView> srv;

        if (FAILED(owner_.device_->CreateShaderResourceView(src, nullptr, &srv))) {
            return std::unexpected(L"Failed to create shader resource view");
        }

        ID3D11ShaderResourceView* const inputs[] = {srv.Get(), owner_.xforms_.srv.Get()};
        owner_.ctx_->PSSetShaderResources(0u, 2u, inputs);
    }

    D3D11_TEXTURE2D_DESC desc{};
    dst_->GetDesc(&desc);

    const D3D11_VIEWPORT vp{0.0f, 0.0f, static_cast<float>(desc.Width), static_cast<float>(desc.Height), 0.0f, 1.0f};

    owner_.ctx_->IASetInputLayout(nullptr);
    owner_.ctx_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    owner_.ctx_->VSSetShader(owner_.vs_.Get(), nullptr, 0u);
    owner_.ctx_->PSSetShader(owner_.ps_.Get(), nullptr, 0u);

    owner_.ctx_->RSSetViewports(1u, &vp);

    owner_.ctx_->Draw(3u, 0u);

    owner_.ctx_->OMSetRenderTargets(1u, &null_rtv, nullptr);
    owner_.ctx_->PSSetShaderResources(0u, 2u, null_srvs);

    return {};
}

Renderer::Result Renderer::Acquire(ID3D11Texture2D* tex) {
    {
        ComPtr<ID3D11Device> device;
        tex->GetDevice(&device);

        if (device_ != nullptr && device_ == device) {
            return {};
        }

        Reset();

        device_ = std::move(device);
        device_->GetImmediateContext(&ctx_);
    }

    if (FAILED(device_->CreateVertexShader(g_fullscreen, sizeof(g_fullscreen), nullptr, &vs_))) {
        Reset();
        return std::unexpected(L"Failed to create vertex shader");
    }

    if (FAILED(device_->CreatePixelShader(g_blur, sizeof(g_blur), nullptr, &ps_))) {
        Reset();
        return std::unexpected(L"Failed to create pixel shader");
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

        if (FAILED(device_->CreateSamplerState(&desc, &smp_))) {
            Reset();
            return std::unexpected(L"Failed to create sampler state");
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

        if (FAILED(device_->CreateBuffer(&desc, nullptr, &cb_))) {
            Reset();
            return std::unexpected(L"Failed to create constant buffer");
        }
    }

    return {};
}

void Renderer::Reset() {
    xforms_.buffer.Reset();
    xforms_.srv.Reset();
    xforms_.size = 0uz;

    cb_.Reset();
    smp_.Reset();
    ps_.Reset();
    vs_.Reset();

    ctx_.Reset();
    device_.Reset();
}
}  // namespace blur::object::direct3d
