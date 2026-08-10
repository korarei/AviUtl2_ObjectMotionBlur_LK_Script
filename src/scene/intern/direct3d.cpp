#include "direct3d.hpp"

#include <algorithm>
#include <cstring>

#include <intern/aviutl/aviutl.hpp>

#include <blur.h>
#include <classify_layer.h>
#include <convert.h>
#include <debug.h>
#include <decode_flow.h>
#include <fullscreen.h>
#include <propagate_layer.h>
#include <reduce_layer.h>
#include <regularize_flow.h>

namespace blur::scene::direct3d {
namespace {
class Direct3DErrorCategory final : public std::error_category {
  public:
    [[nodiscard]] const char* name() const noexcept override { return "direct3d.nvidia_optical_flow"; }

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
            case Error::kOpticalFlowUnavailable:
                return "NVIDIA Optical Flow is not available";
            case Error::kInvalidOpticalFlowCall:
                return "NVIDIA Optical Flow API was called in an invalid order";
            case Error::kFlowAlreadyConsumed:
                return "Optical flow has already been consumed";
            case Error::kIncompatibleOpticalFlowVersion:
                return "NVIDIA Optical Flow API version is incompatible";
            case Error::kOpticalFlowNotInitialized:
                return "NVIDIA Optical Flow is not initialized";
            case Error::kOpticalFlowInternalError:
                return "NVIDIA Optical Flow reported an internal error";
            case Error::kOpticalFlowDirect3DFailed:
                return "Direct3D operation in NVIDIA Optical Flow failed";
            case Error::kUnknownOpticalFlowException:
                return "Unknown NVIDIA Optical Flow exception";
            case Error::kNoSupportedOutputGridSize:
                return "No supported NVIDIA Optical Flow output grid size was found";
            case Error::kRegisterOpticalFlowInputTextureFailed:
                return "Failed to register NVIDIA Optical Flow input texture";
        }

        return "Unknown Direct3D and NVIDIA Optical Flow error";
    }
};

constinit const Direct3DErrorCategory kErrorCategory{};

constexpr uint32_t kLayerTileSize = 16u;

std::error_code ToErrorCode(HRESULT result, Error fallback) {
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

std::error_code ToErrorCode(NV_OF_STATUS status) {
    switch (status) {
        case NV_OF_SUCCESS:
            return Error::kOpticalFlowInternalError;
        case NV_OF_ERR_INVALID_PTR:
        case NV_OF_ERR_INVALID_PARAM:
            return std::make_error_code(std::errc::invalid_argument);
        case NV_OF_ERR_OUT_OF_MEMORY:
            return std::make_error_code(std::errc::not_enough_memory);
        case NV_OF_ERR_DEVICE_DOES_NOT_EXIST:
            return std::make_error_code(std::errc::no_such_device);
        case NV_OF_ERR_UNSUPPORTED_DEVICE:
        case NV_OF_ERR_UNSUPPORTED_FEATURE:
            return std::make_error_code(std::errc::not_supported);
        case NV_OF_ERR_OF_NOT_AVAILABLE:
            return Error::kOpticalFlowUnavailable;
        case NV_OF_ERR_INVALID_CALL:
            return Error::kInvalidOpticalFlowCall;
        case NV_OF_ERR_INVALID_VERSION:
            return Error::kIncompatibleOpticalFlowVersion;
        case NV_OF_ERR_NOT_INITIALIZED:
            return Error::kOpticalFlowNotInitialized;
        case NV_OF_ERR_GENERIC:
            return Error::kOpticalFlowInternalError;
    }
    return Error::kOpticalFlowInternalError;
}
}  // namespace

std::error_code make_error_code(Error error) noexcept { return {static_cast<int>(error), kErrorCategory}; }

std::expected<Renderer::Flow, std::error_code> Renderer::Context::ComputeFlow(ID3D11Texture2D* src,
                                                                              ID3D11Texture2D* depth,
                                                                              const Param& param) const {
    try {
        struct NvOutput {
            NvOFBufferObj buf = nullptr;
            ComPtr<ID3D11Texture2D> tex = nullptr;
            ComPtr<ID3D11ShaderResourceView> srv = nullptr;
        };

        auto& session = owner_.sessions_[param.id];

        {
            D3D11_TEXTURE2D_DESC desc{};
            src->GetDesc(&desc);

            if (session.ctx == nullptr || session.w != desc.Width || session.h != desc.Height) {
                auto result = owner_.CreateFlowSession(desc.Width, desc.Height, param.preset);
                if (result.has_value()) {
                    session = std::move(*result);
                } else {
                    owner_.sessions_.erase(param.id);
                    return std::unexpected(result.error());
                }
            }
        }

        HRESULT result;
        std::error_code error_code;

        Flow flow;
        flow.w = session.w;
        flow.h = session.h;
        flow.grid_size = session.grid_size;

        std::array<NvOutput, 2uz> nv_flows{};
        std::array<NvOutput, 2uz> nv_costs{};

        const D3D11_VIEWPORT vp{
            .TopLeftX = 0.0f,
            .TopLeftY = 0.0f,
            .Width = static_cast<float>(session.w),
            .Height = static_cast<float>(session.h),
            .MinDepth = 0.0f,
            .MaxDepth = 1.0f,
        };

        auto& prev = session.inputs[0uz];
        auto& curr = session.inputs[1uz];

        if (curr.frame.has_value() && *curr.frame != param.frame) {
            std::swap(prev, curr);
        }

        flow.src_.tex = src;

        result = owner_.device_->CreateShaderResourceView(src, nullptr, &flow.src_.srv);
        if (FAILED(result)) {
            owner_.sessions_.erase(param.id);
            return std::unexpected(ToErrorCode(result, Error::kCreateShaderResourceViewFailed));
        }

        {
            auto bufs = session.ctx->CreateBuffers(NV_OF_BUFFER_USAGE_OUTPUT, static_cast<uint32_t>(nv_flows.size()));
            if (bufs.size() != nv_flows.size()) {
                owner_.sessions_.erase(param.id);
                return std::unexpected(make_error_code(Error::kCreateTexture2DFailed));
            }

            for (size_t i = 0uz; i < nv_flows.size(); ++i) {
                auto& nv_flow = nv_flows[i];
                nv_flow.buf = std::move(bufs[i]);
                if (nv_flow.buf == nullptr) {
                    owner_.sessions_.erase(param.id);
                    return std::unexpected(make_error_code(Error::kCreateTexture2DFailed));
                }

                auto* const buf = static_cast<NvOFBufferD3D11*>(nv_flow.buf.get());
                nv_flow.tex = buf->getD3D11TextureHandle();
                result = owner_.device_->CreateShaderResourceView(nv_flow.tex.Get(), nullptr, &nv_flow.srv);
                if (FAILED(result)) {
                    owner_.sessions_.erase(param.id);
                    return std::unexpected(ToErrorCode(result, Error::kCreateShaderResourceViewFailed));
                }
            }
        }

        {
            auto bufs = session.ctx->CreateBuffers(NV_OF_BUFFER_USAGE_COST, static_cast<uint32_t>(nv_costs.size()));
            if (bufs.size() != nv_costs.size()) {
                owner_.sessions_.erase(param.id);
                return std::unexpected(make_error_code(Error::kCreateTexture2DFailed));
            }

            for (size_t i = 0uz; i < nv_costs.size(); ++i) {
                auto& nv_cost = nv_costs[i];
                nv_cost.buf = std::move(bufs[i]);
                if (nv_cost.buf == nullptr) {
                    owner_.sessions_.erase(param.id);
                    return std::unexpected(make_error_code(Error::kCreateTexture2DFailed));
                }

                auto* const buf = static_cast<NvOFBufferD3D11*>(nv_cost.buf.get());
                result = owner_.device_->CreateShaderResourceView(buf->getD3D11TextureHandle(), nullptr, &nv_cost.srv);
                if (FAILED(result)) {
                    owner_.sessions_.erase(param.id);
                    return std::unexpected(ToErrorCode(result, Error::kCreateShaderResourceViewFailed));
                }
            }
        }

        {
            ComPtr<ID3D11RenderTargetView> rtv;
            result = owner_.device_->CreateRenderTargetView(curr.tex.Get(), nullptr, &rtv);
            if (FAILED(result)) {
                owner_.sessions_.erase(param.id);
                return std::unexpected(ToErrorCode(result, Error::kCreateRenderTargetViewFailed));
            }

            error_code = owner_.ToABGR8(rtv.Get(), flow.src_.srv.Get(), vp);
            if (error_code != std::error_code{}) {
                owner_.sessions_.erase(param.id);
                return std::unexpected(error_code);
            }
        }

        curr.section = param.section;
        curr.frame = param.frame;

        const bool is_boundary = param.frame == 0 || !prev.frame.has_value() || curr.section != prev.section;

        if (is_boundary) {
            owner_.ctx_->CopyResource(prev.tex.Get(), curr.tex.Get());
            prev.section = curr.section;
            prev.frame = curr.frame;
        }

        const auto df = param.frame - *prev.frame;

        // なぜか2回実行すると精度上がる (Temporal Hint が効いてる？)

        session.ctx->Execute(curr.buf.get(), prev.buf.get(), nv_flows[0uz].buf.get(), nullptr, nv_costs[0uz].buf.get(),
                             0u, nullptr, nullptr, 0u, nullptr, is_boundary || df != 1 ? NV_OF_TRUE : NV_OF_FALSE,
                             nv_flows[1uz].buf.get(), nv_costs[1uz].buf.get());

        session.ctx->Execute(curr.buf.get(), prev.buf.get(), nv_flows[0uz].buf.get(), nullptr, nv_costs[0uz].buf.get(),
                             0u, nullptr, nullptr, 0u, nullptr, is_boundary || df != 1 ? NV_OF_TRUE : NV_OF_FALSE,
                             nv_flows[1uz].buf.get(), nv_costs[1uz].buf.get());

        FlowParam flow_param{
            .resolution = {session.w, session.h},
            .grid_size = session.grid_size,
            .flow_scale = df == 0 ? 0.0f : 1.0f / static_cast<float>(df),
        };

        const std::array<ID3D11ShaderResourceView*, 4uz> flows = {
            nv_flows[0uz].srv.Get(),
            nv_flows[1uz].srv.Get(),
            nv_costs[0uz].srv.Get(),
            nv_costs[1uz].srv.Get(),
        };

        ComPtr<ID3D11ShaderResourceView> depth_srv;
        result = owner_.device_->CreateShaderResourceView(depth, nullptr, &depth_srv);
        if (FAILED(result)) {
            owner_.sessions_.erase(param.id);
            return std::unexpected(ToErrorCode(result, Error::kCreateShaderResourceViewFailed));
        }

        auto output = owner_.CreateFlowOutput(flows, flow.src_.srv.Get(), depth_srv.Get(), flow_param);
        if (!output.has_value()) {
            owner_.sessions_.erase(param.id);
            return std::unexpected(output.error());
        }

        flow.output_ = std::move(*output);

        return flow;
    } catch (const NvOFException& exception) {
        owner_.sessions_.erase(param.id);
        return std::unexpected(ToErrorCode(exception.getErrorCode()));
    } catch (const std::bad_alloc&) {
        owner_.sessions_.erase(param.id);
        return std::unexpected(std::make_error_code(std::errc::not_enough_memory));
    } catch (const std::invalid_argument&) {
        owner_.sessions_.erase(param.id);
        return std::unexpected(std::make_error_code(std::errc::invalid_argument));
    } catch (const DXException&) {
        owner_.sessions_.erase(param.id);
        return std::unexpected(make_error_code(Error::kOpticalFlowDirect3DFailed));
    } catch (const std::exception&) {
        owner_.sessions_.erase(param.id);
        return std::unexpected(make_error_code(Error::kOpticalFlowInternalError));
    } catch (...) {
        owner_.sessions_.erase(param.id);
        return std::unexpected(make_error_code(Error::kUnknownOpticalFlowException));
    }
}

std::error_code Renderer::Context::Draw(const Flow& flow, const Param& param) const {
    static constexpr ID3D11ShaderResourceView* null_srvs[5uz] = {nullptr, nullptr, nullptr, nullptr, nullptr};

    {
        D3D11_TEXTURE2D_DESC desc{};
        dst_->GetDesc(&desc);

        if (desc.Width != flow.w || desc.Height != flow.h) {
            return std::make_error_code(std::errc::invalid_argument);
        }
    }

    auto layer_tiles = owner_.CreateLayerTiles(flow, param);
    if (!layer_tiles.has_value()) {
        return layer_tiles.error();
    }

    HRESULT result;

    const float w = static_cast<float>(flow.w), h = static_cast<float>(flow.h);

    const float mix = param.mix * 2.0f;
    DrawParam draw_param{
        .texel =
            {
                1.0f / w,
                1.0f / h,
            },
        .shutter =
            {
                param.phase,
                param.phase + param.amount,
            },
        .mix =
            {
                std::min(2.0f - mix, 1.0f),
                std::min(mix, 1.0f),
            },
        .falloff = param.falloff,
        .sample_limit = param.sample_limit,
    };

    ID3D11ShaderResourceView* const inputs[] = {
        flow.src_.srv.Get(),           flow.output_.regularized.srv.Get(), flow.output_.classified.srv.Get(),
        (*layer_tiles)[0uz].srv.Get(), (*layer_tiles)[1uz].srv.Get(),
    };

    ComPtr<ID3D11RenderTargetView> rtv;
    result = owner_.device_->CreateRenderTargetView(dst_, nullptr, &rtv);
    if (FAILED(result)) {
        return ToErrorCode(result, Error::kCreateRenderTargetViewFailed);
    }

    const D3D11_VIEWPORT vp{
        .TopLeftX = 0.0f,
        .TopLeftY = 0.0f,
        .Width = w,
        .Height = h,
        .MinDepth = 0.0f,
        .MaxDepth = 1.0f,
    };

    {
        D3D11_MAPPED_SUBRESOURCE mapped{};

        result = owner_.ctx_->Map(owner_.cb_.Get(), 0u, D3D11_MAP_WRITE_DISCARD, 0u, &mapped);
        if (FAILED(result)) {
            return ToErrorCode(result, Error::kMapBufferFailed);
        }

        std::memcpy(mapped.pData, &draw_param, sizeof(draw_param));
        owner_.ctx_->Unmap(owner_.cb_.Get(), 0u);

        owner_.ctx_->PSSetConstantBuffers(0u, 1u, owner_.cb_.GetAddressOf());
    }

    owner_.ctx_->IASetInputLayout(nullptr);
    owner_.ctx_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    owner_.ctx_->VSSetShader(owner_.vs_.Get(), nullptr, 0u);

    owner_.ctx_->PSSetShader(owner_.ps_.blur.Get(), nullptr, 0u);
    owner_.ctx_->PSSetShaderResources(0u, 5u, inputs);
    owner_.ctx_->PSSetSamplers(0u, 1u, owner_.smp_.GetAddressOf());

    owner_.ctx_->OMSetRenderTargets(1u, rtv.GetAddressOf(), nullptr);

    owner_.ctx_->RSSetViewports(1u, &vp);

    owner_.ctx_->Draw(3u, 0u);

    owner_.ctx_->OMSetRenderTargets(0u, nullptr, nullptr);
    owner_.ctx_->PSSetShaderResources(0u, 5u, null_srvs);

    return {};
}

std::error_code Renderer::Context::Debug(const Flow& flow, const Param& param) const {
    static constexpr ID3D11ShaderResourceView* null_srvs[4uz] = {nullptr, nullptr, nullptr, nullptr};

    {
        D3D11_TEXTURE2D_DESC desc{};
        dst_->GetDesc(&desc);

        if (desc.Width != flow.w || desc.Height != flow.h) {
            return std::make_error_code(std::errc::invalid_argument);
        }
    }

    auto layer_tiles = owner_.CreateLayerTiles(flow, param);
    if (!layer_tiles.has_value()) {
        return layer_tiles.error();
    }

    HRESULT result;

    const float w = static_cast<float>(flow.w), h = static_cast<float>(flow.h);

    DebugParam debug_param{
        .texel =
            {
                1.0f / w,
                1.0f / h,
            },
        .mode = param.view_mode,
        .scale = 0.01f,
    };

    ID3D11ShaderResourceView* const inputs[] = {
        flow.output_.regularized.srv.Get(),
        flow.output_.classified.srv.Get(),
        (*layer_tiles)[0uz].srv.Get(),
        (*layer_tiles)[1uz].srv.Get(),
    };

    ComPtr<ID3D11RenderTargetView> rtv;
    result = owner_.device_->CreateRenderTargetView(dst_, nullptr, &rtv);
    if (FAILED(result)) {
        return ToErrorCode(result, Error::kCreateRenderTargetViewFailed);
    }

    const D3D11_VIEWPORT vp{
        .TopLeftX = 0.0f,
        .TopLeftY = 0.0f,
        .Width = w,
        .Height = h,
        .MinDepth = 0.0f,
        .MaxDepth = 1.0f,
    };

    {
        D3D11_MAPPED_SUBRESOURCE mapped{};

        result = owner_.ctx_->Map(owner_.cb_.Get(), 0u, D3D11_MAP_WRITE_DISCARD, 0u, &mapped);
        if (FAILED(result)) {
            return ToErrorCode(result, Error::kMapBufferFailed);
        }

        std::memcpy(mapped.pData, &debug_param, sizeof(DebugParam));
        owner_.ctx_->Unmap(owner_.cb_.Get(), 0u);

        owner_.ctx_->PSSetConstantBuffers(0u, 1u, owner_.cb_.GetAddressOf());
    }

    owner_.ctx_->IASetInputLayout(nullptr);
    owner_.ctx_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    owner_.ctx_->VSSetShader(owner_.vs_.Get(), nullptr, 0u);

    owner_.ctx_->PSSetShader(owner_.ps_.debug.Get(), nullptr, 0u);
    owner_.ctx_->PSSetShaderResources(0u, 4u, inputs);

    owner_.ctx_->OMSetRenderTargets(1u, rtv.GetAddressOf(), nullptr);

    owner_.ctx_->RSSetViewports(1u, &vp);

    owner_.ctx_->Draw(3u, 0u);

    owner_.ctx_->OMSetRenderTargets(0u, nullptr, nullptr);
    owner_.ctx_->PSSetShaderResources(0u, 4u, null_srvs);

    return {};
}

std::error_code Renderer::Acquire(ID3D11Texture2D* tex) {
    {
        ComPtr<ID3D11Device> device;
        tex->GetDevice(&device);
        if (device_ != nullptr && device_.Get() == device.Get()) {
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

    result = device_->CreatePixelShader(g_convert, sizeof(g_convert), nullptr, &ps_.convert);
    if (FAILED(result)) {
        Release();
        return ToErrorCode(result, Error::kCreatePixelShaderFailed);
    }

    result = device_->CreatePixelShader(g_decode_flow, sizeof(g_decode_flow), nullptr, &ps_.decode_flow);
    if (FAILED(result)) {
        Release();
        return ToErrorCode(result, Error::kCreatePixelShaderFailed);
    }

    result = device_->CreateComputeShader(g_regularize_flow, sizeof(g_regularize_flow), nullptr, &cs_.regularize_flow);
    if (FAILED(result)) {
        Release();
        return ToErrorCode(result, Error::kCreateComputeShaderFailed);
    }

    result = device_->CreatePixelShader(g_classify_layer, sizeof(g_classify_layer), nullptr, &ps_.classify_layer);
    if (FAILED(result)) {
        Release();
        return ToErrorCode(result, Error::kCreatePixelShaderFailed);
    }

    result = device_->CreatePixelShader(g_reduce_layer, sizeof(g_reduce_layer), nullptr, &ps_.reduce_layer);
    if (FAILED(result)) {
        Release();
        return ToErrorCode(result, Error::kCreatePixelShaderFailed);
    }

    result = device_->CreatePixelShader(g_propagate_layer, sizeof(g_propagate_layer), nullptr, &ps_.propagate_layer);
    if (FAILED(result)) {
        Release();
        return ToErrorCode(result, Error::kCreatePixelShaderFailed);
    }

    result = device_->CreatePixelShader(g_blur, sizeof(g_blur), nullptr, &ps_.blur);
    if (FAILED(result)) {
        Release();
        return ToErrorCode(result, Error::kCreatePixelShaderFailed);
    }

    result = device_->CreatePixelShader(g_debug, sizeof(g_debug), nullptr, &ps_.debug);
    if (FAILED(result)) {
        Release();
        return ToErrorCode(result, Error::kCreatePixelShaderFailed);
    }

    {
        constexpr D3D11_SAMPLER_DESC desc{
            .Filter = D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT,
            .AddressU = D3D11_TEXTURE_ADDRESS_CLAMP,
            .AddressV = D3D11_TEXTURE_ADDRESS_CLAMP,
            .AddressW = D3D11_TEXTURE_ADDRESS_CLAMP,
            .MipLODBias = 0.0f,
            .MaxAnisotropy = 1u,
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
        constexpr size_t size = std::max({
            sizeof(FlowParam),
            sizeof(LayerPropagationParam),
            sizeof(DrawParam),
            sizeof(DebugParam),
        });

        constexpr D3D11_BUFFER_DESC desc{
            .ByteWidth = static_cast<uint32_t>(size),
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

std::expected<Renderer::Flow::Session, std::error_code> Renderer::CreateFlowSession(uint32_t w, uint32_t h,
                                                                                    NV_OF_PERF_LEVEL level) const {
    Flow::Session session;

    session.ctx =
        NvOFD3D11::Create(device_.Get(), ctx_.Get(), w, h, NV_OF_BUFFER_FORMAT_ABGR8, NV_OF_MODE_OPTICALFLOW, level);

    {
        uint32_t size = 1u;

        if (!session.ctx->CheckGridSize(1u) && !session.ctx->GetNextMinGridSize(1u, size)) {
            return std::unexpected(make_error_code(Error::kNoSupportedOutputGridSize));
        }

        session.ctx->Init(size, NV_OF_HINT_VECTOR_GRID_SIZE_UNDEFINED, false, false, true, NV_OF_PRED_DIRECTION_BOTH);
        session.grid_size = size;
    }

    {
        auto bufs = session.ctx->CreateBuffers(NV_OF_BUFFER_USAGE_INPUT, static_cast<uint32_t>(session.inputs.size()));
        if (bufs.size() != session.inputs.size()) {
            return std::unexpected(make_error_code(Error::kCreateTexture2DFailed));
        }

        for (size_t i = 0uz; i < session.inputs.size(); ++i) {
            auto& input = session.inputs[i];
            input.buf = std::move(bufs[i]);
            if (input.buf == nullptr) {
                return std::unexpected(make_error_code(Error::kCreateTexture2DFailed));
            }

            auto* const buf = static_cast<NvOFBufferD3D11*>(input.buf.get());
            input.tex = buf->getD3D11TextureHandle();
        }
    }

    session.w = w;
    session.h = h;

    return session;
}

std::expected<Renderer::Flow::Output, std::error_code> Renderer::CreateFlowOutput(
    const std::array<ID3D11ShaderResourceView*, 4uz>& flows, ID3D11ShaderResourceView* src,
    ID3D11ShaderResourceView* depth, const FlowParam& param) const {
    static constexpr ID3D11ShaderResourceView* null_srvs[4uz] = {nullptr, nullptr, nullptr, nullptr};
    static constexpr ID3D11UnorderedAccessView* null_uav = nullptr;

    const auto& [w, h] = param.resolution;

    Flow::Output output{};

    HRESULT result;

    {
        const D3D11_TEXTURE2D_DESC desc{
            .Width = w,
            .Height = h,
            .MipLevels = 1u,
            .ArraySize = 1u,
            .Format = DXGI_FORMAT_R32G32B32A32_FLOAT,
            .SampleDesc = {.Count = 1u, .Quality = 0u},
            .Usage = D3D11_USAGE_DEFAULT,
            .BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS,
            .CPUAccessFlags = 0u,
            .MiscFlags = 0u,
        };

        const auto create = [this, &desc](Texture& dst) -> std::error_code {
            HRESULT result = device_->CreateTexture2D(&desc, nullptr, &dst.tex);
            if (FAILED(result)) {
                return ToErrorCode(result, Error::kCreateTexture2DFailed);
            }

            result = device_->CreateShaderResourceView(dst.tex.Get(), nullptr, &dst.srv);
            if (FAILED(result)) {
                return ToErrorCode(result, Error::kCreateShaderResourceViewFailed);
            }

            return {};
        };

        if (const auto ec = create(output.decoded); ec != std::error_code{}) {
            return std::unexpected(ec);
        }

        if (const auto ec = create(output.regularized); ec != std::error_code{}) {
            return std::unexpected(ec);
        }

        if (const auto ec = create(output.classified); ec != std::error_code{}) {
            return std::unexpected(ec);
        }
    }

    {
        const D3D11_VIEWPORT vp{
            .TopLeftX = 0.0f,
            .TopLeftY = 0.0f,
            .Width = static_cast<float>(w),
            .Height = static_cast<float>(h),
            .MinDepth = 0.0f,
            .MaxDepth = 1.0f,
        };

        ctx_->RSSetViewports(1u, &vp);
    }

    {
        D3D11_MAPPED_SUBRESOURCE mapped{};

        result = ctx_->Map(cb_.Get(), 0u, D3D11_MAP_WRITE_DISCARD, 0u, &mapped);
        if (FAILED(result)) {
            return std::unexpected(ToErrorCode(result, Error::kMapBufferFailed));
        }

        std::memcpy(mapped.pData, &param, sizeof(param));
        ctx_->Unmap(cb_.Get(), 0u);

        ctx_->PSSetConstantBuffers(0u, 1u, cb_.GetAddressOf());
        ctx_->CSSetConstantBuffers(0u, 1u, cb_.GetAddressOf());
    }

    ctx_->IASetInputLayout(nullptr);
    ctx_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    ctx_->VSSetShader(vs_.Get(), nullptr, 0u);

    ctx_->PSSetSamplers(0u, 1u, smp_.GetAddressOf());

    {
        ComPtr<ID3D11RenderTargetView> rtv;
        result = device_->CreateRenderTargetView(output.decoded.tex.Get(), nullptr, &rtv);
        if (FAILED(result)) {
            return std::unexpected(ToErrorCode(result, Error::kCreateRenderTargetViewFailed));
        }

        ctx_->PSSetShader(ps_.decode_flow.Get(), nullptr, 0u);
        ctx_->PSSetShaderResources(0u, static_cast<UINT>(flows.size()), flows.data());

        ctx_->OMSetRenderTargets(1u, rtv.GetAddressOf(), nullptr);

        ctx_->Draw(3u, 0u);

        ctx_->OMSetRenderTargets(0u, nullptr, nullptr);
        ctx_->PSSetShaderResources(0u, 4u, null_srvs);
    }

    {
        ID3D11ShaderResourceView* const inputs[2uz] = {output.decoded.srv.Get(), src};

        ComPtr<ID3D11UnorderedAccessView> uav;
        result = device_->CreateUnorderedAccessView(output.regularized.tex.Get(), nullptr, &uav);
        if (FAILED(result)) {
            return std::unexpected(ToErrorCode(result, Error::kCreateUnorderedAccessViewFailed));
        }

        ctx_->CSSetShader(cs_.regularize_flow.Get(), nullptr, 0u);
        ctx_->CSSetShaderResources(0u, 2u, inputs);
        ctx_->CSSetUnorderedAccessViews(0u, 1u, uav.GetAddressOf(), nullptr);

        ctx_->Dispatch((w + 7u) / 8u, (h + 7u) / 8u, 1u);

        ctx_->CSSetUnorderedAccessViews(0u, 1u, &null_uav, nullptr);
        ctx_->CSSetShaderResources(0u, 2u, null_srvs);
    }

    {
        ID3D11ShaderResourceView* const inputs[3uz] = {output.regularized.srv.Get(), src, depth};

        ComPtr<ID3D11RenderTargetView> rtv;
        result = device_->CreateRenderTargetView(output.classified.tex.Get(), nullptr, &rtv);
        if (FAILED(result)) {
            return std::unexpected(ToErrorCode(result, Error::kCreateRenderTargetViewFailed));
        }

        ctx_->PSSetShader(ps_.classify_layer.Get(), nullptr, 0u);
        ctx_->PSSetShaderResources(0u, 3u, inputs);

        ctx_->OMSetRenderTargets(1u, rtv.GetAddressOf(), nullptr);

        ctx_->Draw(3u, 0u);

        ctx_->OMSetRenderTargets(0u, nullptr, nullptr);
        ctx_->PSSetShaderResources(0u, 3u, null_srvs);
    }

    return output;
}

std::expected<std::array<Renderer::Texture, 2uz>, std::error_code> Renderer::CreateLayerTiles(
    const Flow& flow, const Param& param) const {
    static constexpr ID3D11ShaderResourceView* null_srvs[2uz] = {nullptr, nullptr};

    struct Tile {
        Texture output{};
        ComPtr<ID3D11RenderTargetView> rtv = nullptr;
    } tiles[2uz][2uz]{};

    const uint32_t tile_w = (flow.w + kLayerTileSize - 1u) / kLayerTileSize;
    const uint32_t tile_h = (flow.h + kLayerTileSize - 1u) / kLayerTileSize;

    {
        const D3D11_TEXTURE2D_DESC desc{
            .Width = tile_w,
            .Height = tile_h,
            .MipLevels = 1u,
            .ArraySize = 1u,
            .Format = DXGI_FORMAT_R32G32B32A32_FLOAT,
            .SampleDesc = {.Count = 1u, .Quality = 0u},
            .Usage = D3D11_USAGE_DEFAULT,
            .BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE,
            .CPUAccessFlags = 0u,
            .MiscFlags = 0u,
        };

        const auto create = [this, &desc](Tile& dst) -> std::error_code {
            HRESULT result = device_->CreateTexture2D(&desc, nullptr, &dst.output.tex);
            if (FAILED(result)) {
                return ToErrorCode(result, Error::kCreateTexture2DFailed);
            }

            result = device_->CreateShaderResourceView(dst.output.tex.Get(), nullptr, &dst.output.srv);
            if (FAILED(result)) {
                return ToErrorCode(result, Error::kCreateShaderResourceViewFailed);
            }

            result = device_->CreateRenderTargetView(dst.output.tex.Get(), nullptr, &dst.rtv);
            if (FAILED(result)) {
                return ToErrorCode(result, Error::kCreateRenderTargetViewFailed);
            }

            return {};
        };

        for (auto& buf : tiles) {
            for (auto& tile : buf) {
                if (const auto ec = create(tile); ec != std::error_code{}) {
                    return std::unexpected(ec);
                }
            }
        }
    }

    {
        const D3D11_VIEWPORT vp{
            .TopLeftX = 0.0f,
            .TopLeftY = 0.0f,
            .Width = static_cast<float>(tile_w),
            .Height = static_cast<float>(tile_h),
            .MinDepth = 0.0f,
            .MaxDepth = 1.0f,
        };

        ctx_->RSSetViewports(1u, &vp);
    }

    ctx_->IASetInputLayout(nullptr);
    ctx_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    ctx_->VSSetShader(vs_.Get(), nullptr, 0u);

    {
        ID3D11RenderTargetView* const outputs[] = {
            tiles[0uz][0uz].rtv.Get(),
            tiles[0uz][1uz].rtv.Get(),
        };
        ID3D11ShaderResourceView* const inputs[] = {
            flow.output_.classified.srv.Get(),
            flow.src_.srv.Get(),
        };

        ctx_->PSSetShader(ps_.reduce_layer.Get(), nullptr, 0u);
        ctx_->PSSetShaderResources(0u, 2u, inputs);

        ctx_->OMSetRenderTargets(2u, outputs, nullptr);

        ctx_->Draw(3u, 0u);

        ctx_->OMSetRenderTargets(0u, nullptr, nullptr);
        ctx_->PSSetShaderResources(0u, 2u, null_srvs);
    }

    ctx_->PSSetShader(ps_.propagate_layer.Get(), nullptr, 0u);

    uint32_t curr = 0u;

    LayerPropagationParam prop_param{
        .resolution =
            {
                static_cast<float>(flow.w),
                static_cast<float>(flow.h),
            },
        .shutter =
            {
                param.phase,
                param.phase + param.amount,
            },
        .step = 1,
    };

    const uint32_t range = std::max(tile_w, tile_h);
    for (uint32_t step = 1u, dist = 0u; dist < range; step <<= 1u) {
        const uint32_t next = 1u - curr;
        ID3D11RenderTargetView* const outputs[] = {
            tiles[next][0uz].rtv.Get(),
            tiles[next][1uz].rtv.Get(),
        };
        ID3D11ShaderResourceView* const inputs[] = {
            tiles[curr][0uz].output.srv.Get(),
            tiles[curr][1uz].output.srv.Get(),
        };

        prop_param.step = static_cast<int32_t>(step);

        {
            D3D11_MAPPED_SUBRESOURCE mapped{};

            HRESULT result = ctx_->Map(cb_.Get(), 0u, D3D11_MAP_WRITE_DISCARD, 0u, &mapped);
            if (FAILED(result)) {
                return std::unexpected(ToErrorCode(result, Error::kMapBufferFailed));
            }

            std::memcpy(mapped.pData, &prop_param, sizeof(prop_param));
            ctx_->Unmap(cb_.Get(), 0u);

            ctx_->PSSetConstantBuffers(0u, 1u, cb_.GetAddressOf());
        }

        ctx_->PSSetShaderResources(0u, 2u, inputs);

        ctx_->OMSetRenderTargets(2u, outputs, nullptr);

        ctx_->Draw(3u, 0u);

        ctx_->OMSetRenderTargets(0u, nullptr, nullptr);
        ctx_->PSSetShaderResources(0u, 2u, null_srvs);

        curr = next;
        dist += step;
    }

    std::array<Texture, 2uz> output{};
    for (size_t i = 0uz; i < output.size(); ++i) {
        output[i] = std::move(tiles[curr][i].output);
    }

    return output;
}

std::error_code Renderer::ToABGR8(ID3D11RenderTargetView* dst, ID3D11ShaderResourceView* src,
                                  const D3D11_VIEWPORT& vp) const {
    static constexpr ID3D11ShaderResourceView* null_srv = nullptr;

    ctx_->IASetInputLayout(nullptr);
    ctx_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    ctx_->VSSetShader(vs_.Get(), nullptr, 0u);

    ctx_->PSSetShader(ps_.convert.Get(), nullptr, 0u);
    ctx_->PSSetShaderResources(0u, 1u, &src);

    ctx_->OMSetRenderTargets(1u, &dst, nullptr);

    ctx_->RSSetViewports(1u, &vp);

    ctx_->Draw(3u, 0u);

    ctx_->OMSetRenderTargets(0u, nullptr, nullptr);
    ctx_->PSSetShaderResources(0u, 1u, &null_srv);

    return {};
}

void Renderer::Reset() {
    const std::lock_guard lock(mutex_);
    Release();
}

void Renderer::Release() {
    std::unordered_map<int64_t, Flow::Session>{}.swap(sessions_);

    cb_.Reset();
    smp_.Reset();
    ps_.debug.Reset();
    ps_.blur.Reset();
    ps_.propagate_layer.Reset();
    ps_.reduce_layer.Reset();
    ps_.classify_layer.Reset();
    ps_.decode_flow.Reset();
    ps_.convert.Reset();
    vs_.Reset();
    cs_.regularize_flow.Reset();
    dss_.Reset();

    ctx_.Reset();
    device_.Reset();
}
}  // namespace blur::scene::direct3d
